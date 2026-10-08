/** @file
    PSK candidate detection and differential BPSK demodulation.

    Copyright (C) 2026 Kim Bloxsom

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "psk_demod.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    double re;
    double im;
} cpx_t;

static cpx_t c_add(cpx_t a, cpx_t b)
{
    cpx_t r = {a.re + b.re, a.im + b.im};
    return r;
}

static cpx_t c_sub(cpx_t a, cpx_t b)
{
    cpx_t r = {a.re - b.re, a.im - b.im};
    return r;
}

static cpx_t c_mul(cpx_t a, cpx_t b)
{
    cpx_t r = {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
    return r;
}

static cpx_t c_conj(cpx_t a)
{
    cpx_t r = {a.re, -a.im};
    return r;
}

static cpx_t c_scale(cpx_t a, double s)
{
    cpx_t r = {a.re * s, a.im * s};
    return r;
}

static double c_abs2(cpx_t a)
{
    return a.re * a.re + a.im * a.im;
}

static cpx_t c_expj(double phase)
{
    cpx_t r = {cos(phase), sin(phase)};
    return r;
}

static int cmp_double(void const *pa, void const *pb)
{
    double a = *(double const *)pa;
    double b = *(double const *)pb;

    return (a > b) - (a < b);
}

static cpx_t cu8_sample(uint8_t const *iq_buf, size_t sample)
{
    cpx_t value = {
            (double)iq_buf[sample * 2] - 127.5,
            (double)iq_buf[sample * 2 + 1] - 127.5};

    return value;
}

int psk_find_candidate(
        uint8_t const *iq_buf,
        size_t sample_count,
        unsigned sample_size,
        unsigned sample_rate,
        psk_candidate_t *candidate)
{
    size_t block_samples;
    size_t block_count;
    double *block_power;
    double *sorted_power;
    double background;
    double peak;
    double threshold;
    size_t block;
    size_t best_start = 0;
    size_t best_end = 0;
    double best_peak = 0.0;
    cpx_t phase_sum = {0.0, 0.0};
    size_t start_sample;
    size_t end_sample;
    size_t i;

    if (!iq_buf || !candidate || !sample_rate) {
        return 0;
    }

    /* Currently validated for unsigned 8-bit interleaved I/Q only. */
    if (sample_size != 2) {
        return 0;
    }

    /* Half-millisecond blocks smooth the envelope without hiding the burst. */
    block_samples = sample_rate / 2000;
    if (block_samples < 16) {
        block_samples = 16;
    }

    block_count = sample_count / block_samples;
    if (block_count < 3) {
        return 0;
    }

    block_power = malloc(block_count * sizeof(*block_power));
    if (!block_power) {
        return 0;
    }

    sorted_power = malloc(block_count * sizeof(*sorted_power));
    if (!sorted_power) {
        free(block_power);
        return 0;
    }

    peak = 0.0;
    for (block = 0; block < block_count; block++) {
        double sum = 0.0;
        size_t begin = block * block_samples;

        for (i = 0; i < block_samples; i++) {
            sum += c_abs2(cu8_sample(iq_buf, begin + i));
        }
        block_power[block] = sum / (double)block_samples;
        sorted_power[block] = block_power[block];
        if (block_power[block] > peak) {
            peak = block_power[block];
        }
    }

    qsort(sorted_power, block_count, sizeof(*sorted_power), cmp_double);
    background = sorted_power[block_count / 2] + 1e-12;
    free(sorted_power);

    if (peak < background * 2.0) {
        free(block_power);
        return 0;
    }

    threshold = background + 0.30 * (peak - background);

    for (block = 0; block < block_count;) {
        size_t run_start;
        double run_peak = 0.0;

        if (block_power[block] <= threshold) {
            block++;
            continue;
        }

        run_start = block;
        while (block < block_count && block_power[block] > threshold) {
            if (block_power[block] > run_peak) {
                run_peak = block_power[block];
            }
            block++;
        }

        if (block - run_start >= 2 && run_peak > best_peak) {
            best_start = run_start;
            best_end = block;
            best_peak = run_peak;
        }
    }
    free(block_power);

    if (best_end <= best_start) {
        return 0;
    }

    start_sample = best_start * block_samples;
    end_sample = best_end * block_samples;
    if (end_sample > sample_count) {
        end_sample = sample_count;
    }

    for (i = start_sample + 1; i < end_sample; i++) {
        cpx_t previous = cu8_sample(iq_buf, i - 1);
        cpx_t current = cu8_sample(iq_buf, i);
        cpx_t delta = c_mul(current, c_conj(previous));
        double magnitude = sqrt(c_abs2(delta));

        if (magnitude > 0.0) {
            phase_sum = c_add(phase_sum, c_scale(delta, 1.0 / magnitude));
        }
    }

    if (c_abs2(phase_sum) <= 0.0) {
        return 0;
    }

    candidate->offset_s =
            0.5 * (double)(start_sample + end_sample) /
            (double)sample_rate;
    candidate->signature_db = 10.0 * log10(best_peak / background);
    candidate->carrier_offset_hz =
            atan2(phase_sum.im, phase_sum.re) *
            (double)sample_rate / (2.0 * M_PI);
    candidate->symbol_rate_hint = 0.0;

    return 1;
}

static int demod_bits(
        cpx_t const *iq,
        size_t count,
        unsigned sample_rate,
        double symbol_rate,
        double carrier_offset_hz,
        double phase_samples,
        uint8_t **bits_out,
        size_t *nbits_out)
{
    cpx_t *mixed;
    cpx_t *cumulative;
    cpx_t *symbols;
    cpx_t *diff;
    double *magnitudes;
    double samples_per_symbol;
    size_t symbol_count;
    size_t i;
    double threshold;
    cpx_t sq_sum = {0.0, 0.0};
    double residual_phase;
    cpx_t correction;
    uint8_t *bits;

    if (!iq || !bits_out || !nbits_out || !sample_rate ||
            symbol_rate <= 0.0) {
        return 0;
    }

    samples_per_symbol = (double)sample_rate / symbol_rate;
    if ((double)count <= phase_samples + samples_per_symbol * 102.0) {
        return 0;
    }

    symbol_count = (size_t)(((double)count - phase_samples) /
                            samples_per_symbol) - 1;
    if (symbol_count < 100) {
        return 0;
    }

    mixed = malloc(count * sizeof(*mixed));
    if (!mixed) {
        return 0;
    }

    cumulative = calloc(count + 1, sizeof(*cumulative));
    if (!cumulative) {
        free(mixed);
        return 0;
    }

    symbols = malloc(symbol_count * sizeof(*symbols));
    if (!symbols) {
        free(mixed);
        free(cumulative);
        return 0;
    }

    diff = malloc((symbol_count - 1) * sizeof(*diff));
    if (!diff) {
        free(mixed);
        free(cumulative);
        free(symbols);
        return 0;
    }

    magnitudes = malloc((symbol_count - 1) * sizeof(*magnitudes));
    if (!magnitudes) {
        free(mixed);
        free(cumulative);
        free(symbols);
        free(diff);
        return 0;
    }

    bits = malloc((symbol_count - 1) * sizeof(*bits));
    if (!bits) {
        free(mixed);
        free(cumulative);
        free(symbols);
        free(diff);
        free(magnitudes);
        return 0;
    }

    for (i = 0; i < count; i++) {
        double phase = -2.0 * M_PI * carrier_offset_hz *
                (double)i / (double)sample_rate;
        mixed[i] = c_mul(iq[i], c_expj(phase));
    }

    cumulative[0].re = 0.0;
    cumulative[0].im = 0.0;
    for (i = 0; i < count; i++) {
        cumulative[i + 1] = c_add(cumulative[i], mixed[i]);
    }

    for (i = 0; i < symbol_count; i++) {
        long start = lround(phase_samples +
                (double)i * samples_per_symbol);
        long end = lround(phase_samples +
                (double)(i + 1) * samples_per_symbol);
        long width;

        if (start < 0) {
            start = 0;
        }
        if (start > (long)count) {
            start = (long)count;
        }
        if (end < 0) {
            end = 0;
        }
        if (end > (long)count) {
            end = (long)count;
        }

        width = end - start;
        if (width <= 0) {
            free(mixed);
            free(cumulative);
            free(symbols);
            free(diff);
            free(magnitudes);
            free(bits);
            return 0;
        }

        symbols[i] = c_scale(
                c_sub(cumulative[(size_t)end],
                        cumulative[(size_t)start]),
                1.0 / (double)width);
    }

    for (i = 0; i + 1 < symbol_count; i++) {
        diff[i] = c_mul(symbols[i + 1], c_conj(symbols[i]));
        magnitudes[i] = sqrt(c_abs2(diff[i]));
    }

    {
        double *sorted;
        size_t diff_count = symbol_count - 1;
        size_t idx;

        sorted = malloc(diff_count * sizeof(*sorted));
        if (!sorted) {
            free(mixed);
            free(cumulative);
            free(symbols);
            free(diff);
            free(magnitudes);
            free(bits);
            return 0;
        }

        memcpy(sorted, magnitudes, diff_count * sizeof(*sorted));
        qsort(sorted, diff_count, sizeof(*sorted), cmp_double);
        idx = (size_t)(0.20 * (double)diff_count);
        if (idx >= diff_count) {
            idx = diff_count - 1;
        }
        threshold = sorted[idx];
        free(sorted);
    }

    for (i = 0; i + 1 < symbol_count; i++) {
        double mag = magnitudes[i];

        if (mag > threshold && mag > 0.0) {
            cpx_t normalized = c_scale(diff[i], 1.0 / mag);
            sq_sum = c_add(sq_sum, c_mul(normalized, normalized));
        }
    }

    if (c_abs2(sq_sum) <= 0.0) {
        free(mixed);
        free(cumulative);
        free(symbols);
        free(diff);
        free(magnitudes);
        free(bits);
        return 0;
    }

    residual_phase = 0.5 * atan2(sq_sum.im, sq_sum.re);
    correction = c_expj(-residual_phase);

    for (i = 0; i + 1 < symbol_count; i++) {
        cpx_t corrected = c_mul(diff[i], correction);
        bits[i] = corrected.re >= 0.0 ? 1 : 0;
    }

    *bits_out = bits;
    *nbits_out = symbol_count - 1;

    free(mixed);
    free(cumulative);
    free(symbols);
    free(diff);
    free(magnitudes);

    return 1;
}

static int validate_bits(
        uint8_t const *bits,
        size_t nbits,
        int invert,
        psk_validate_fn validate_fn,
        void *validate_context,
        bitbuffer_t *bitbuffer)
{
    size_t i;

    if (!bits || !nbits || !validate_fn || !bitbuffer) {
        return 0;
    }

    bitbuffer_clear(bitbuffer);

    for (i = 0; i < nbits; i++) {
        bitbuffer_add_bit(bitbuffer,
                invert ? (uint8_t)!bits[i] : bits[i]);
    }

    if (validate_fn(bitbuffer, validate_context) > 0) {
        return 1;
    }

    bitbuffer_clear(bitbuffer);
    return 0;
}

int psk_demod_candidate(
        uint8_t const *iq_buf,
        size_t sample_count,
        unsigned sample_size,
        unsigned sample_rate,
        psk_candidate_t const *candidate,
        psk_validate_fn validate_fn,
        void *validate_context,
        bitbuffer_t *bitbuffer)
{
    size_t center;
    size_t half;
    size_t start;
    size_t end;
    size_t candidate_count;
    cpx_t *iq;
    double rates[41];
    int rate_count = 0;
    int delta;
    int r;
    size_t i;

    if (!iq_buf || !candidate || !validate_fn ||
            !bitbuffer || !sample_rate) {
        return 0;
    }

    if (sample_size != 2) {
        return 0;
    }

    center = (size_t)llround(candidate->offset_s *
            (double)sample_rate);
    if (center > sample_count) {
        center = sample_count;
    }

    half = (size_t)(0.012 * (double)sample_rate);
    start = center > half ? center - half : 0;
    end = center + half < sample_count ? center + half : sample_count;

    if (end <= start ||
            end - start < (size_t)(0.015 * (double)sample_rate)) {
        return 0;
    }

    candidate_count = end - start;

    iq = malloc(candidate_count * sizeof(*iq));
    if (!iq) {
        return 0;
    }

    for (i = 0; i < candidate_count; i++) {
        iq[i] = cu8_sample(iq_buf, start + i);
    }

    rates[rate_count++] = candidate->symbol_rate_hint;
    for (delta = 50; delta <= 1000; delta += 50) {
        rates[rate_count++] = candidate->symbol_rate_hint - (double)delta;
        rates[rate_count++] = candidate->symbol_rate_hint + (double)delta;
    }

    for (r = 0; r < rate_count; r++) {
        double symbol_rate = rates[r];
        double samples_per_symbol;
        int p;

        if (symbol_rate < 35000.0 || symbol_rate > 37000.0) {
            continue;
        }

        samples_per_symbol = (double)sample_rate / symbol_rate;

        for (p = 0; p < 16; p++) {
            double phase = samples_per_symbol * (double)p / 16.0;
            uint8_t *bits = NULL;
            size_t nbits = 0;

            if (!demod_bits(
                        iq,
                        candidate_count,
                        sample_rate,
                        symbol_rate,
                        candidate->carrier_offset_hz,
                        phase,
                        &bits,
                        &nbits)) {
                continue;
            }

            if (validate_bits(
                        bits,
                        nbits,
                        0,
                        validate_fn,
                        validate_context,
                        bitbuffer)) {
                free(bits);
                free(iq);
                return 1;
            }

            if (validate_bits(
                        bits,
                        nbits,
                        1,
                        validate_fn,
                        validate_context,
                        bitbuffer)) {
                free(bits);
                free(iq);
                return 1;
            }

            free(bits);
        }
    }

    free(iq);
    bitbuffer_clear(bitbuffer);

    return 0;
}
