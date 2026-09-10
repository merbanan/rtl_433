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

static void fft_inplace(cpx_t *a, size_t n)
{
    size_t i;
    size_t j;
    size_t len;

    for (i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;

        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;

        if (i < j) {
            cpx_t t = a[i];
            a[i] = a[j];
            a[j] = t;
        }
    }

    for (len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / (double)len;
        cpx_t wlen = c_expj(ang);

        for (i = 0; i < n; i += len) {
            cpx_t w = {1.0, 0.0};
            size_t half = len >> 1;

            for (j = 0; j < half; j++) {
                cpx_t u = a[i + j];
                cpx_t v = c_mul(a[i + j + half], w);

                a[i + j] = c_add(u, v);
                a[i + j + half] = c_sub(u, v);
                w = c_mul(w, wlen);
            }
        }
    }
}

static int cmp_double(void const *pa, void const *pb)
{
    double a = *(double const *)pa;
    double b = *(double const *)pb;

    return (a > b) - (a < b);
}

static double median_copy(double const *v, size_t n)
{
    double *tmp;
    double result;

    if (!n) {
        return 0.0;
    }

    tmp = malloc(n * sizeof(*tmp));
    if (!tmp) {
        return 0.0;
    }

    memcpy(tmp, v, n * sizeof(*tmp));
    qsort(tmp, n, sizeof(*tmp), cmp_double);

    if (n & 1) {
        result = tmp[n / 2];
    }
    else {
        result = 0.5 * (tmp[n / 2 - 1] + tmp[n / 2]);
    }

    free(tmp);
    return result;
}

static double fft_bin_freq(size_t bin, size_t nfft, double sample_rate)
{
    if (bin <= nfft / 2) {
        return (double)bin * sample_rate / (double)nfft;
    }

    return ((double)bin - (double)nfft) * sample_rate / (double)nfft;
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
    size_t window_samples;
    size_t rows;
    size_t nfft = 1;
    size_t row;
    int found = 0;
    psk_candidate_t global_best = {0};

    if (!iq_buf || !candidate || !sample_rate) {
        return 0;
    }

    /* Currently validated for unsigned 8-bit interleaved I/Q only. */
    if (sample_size != 2) {
        return 0;
    }

    window_samples = (size_t)((double)sample_rate * 0.004);
    if (window_samples < 256) {
        window_samples = 256;
    }

    rows = sample_count / window_samples;
    if (!rows) {
        return 0;
    }

    while (nfft < window_samples) {
        nfft <<= 1;
    }

    for (row = 0; row < rows; row++) {
        cpx_t *buf;
        double *bg;
        size_t bg_count = 0;
        size_t k;
        size_t top_bins[16] = {0};
        double top_power[16] = {0};
        cpx_t mean = {0.0, 0.0};
        double bg_hi;
        double background;
        double row_best_score = 0.0;
        double row_best_midpoint = 0.0;
        double row_best_sep = 0.0;

        buf = calloc(nfft, sizeof(*buf));
        if (!buf) {
            return 0;
        }

        bg = malloc(nfft * sizeof(*bg));
        if (!bg) {
            free(buf);
            return 0;
        }

        for (k = 0; k < window_samples; k++) {
            mean = c_add(mean, cu8_sample(iq_buf, row * window_samples + k));
        }
        mean = c_scale(mean, 1.0 / (double)window_samples);

        for (k = 0; k < window_samples; k++) {
            double w = 0.5 - 0.5 * cos(
                                         2.0 * M_PI * (double)k /
                                         (double)(window_samples - 1));
            cpx_t sample = cu8_sample(iq_buf, row * window_samples + k);

            buf[k] = c_scale(c_sub(sample, mean), w);
        }

        fft_inplace(buf, nfft);

        bg_hi = (double)sample_rate / 2.0 * 0.90;
        if (bg_hi > 110000.0) {
            bg_hi = 110000.0;
        }

        for (k = 0; k < nfft; k++) {
            double f = fft_bin_freq(k, nfft, (double)sample_rate);
            double af = fabs(f);
            double p = c_abs2(buf[k]);

            if (af > 2000.0 && af < 60000.0) {
                int slot;

                for (slot = 0; slot < 16; slot++) {
                    if (p > top_power[slot]) {
                        int s;

                        for (s = 15; s > slot; s--) {
                            top_power[s] = top_power[s - 1];
                            top_bins[s] = top_bins[s - 1];
                        }

                        top_power[slot] = p;
                        top_bins[slot] = k;
                        break;
                    }
                }
            }

            if (af > 60000.0 && af < bg_hi) {
                bg[bg_count++] = p;
            }
        }

        if (bg_count < 5) {
            bg_count = 0;

            for (k = 0; k < nfft; k++) {
                double f = fft_bin_freq(k, nfft, (double)sample_rate);
                double af = fabs(f);

                if (af > 40000.0 &&
                        af < (double)sample_rate / 2.0 * 0.90) {
                    bg[bg_count++] = c_abs2(buf[k]);
                }
            }
        }

        if (bg_count >= 5) {
            int a;
            int b;

            background = median_copy(bg, bg_count) + 1e-12;

            for (a = 0; a < 16; a++) {
                if (top_power[a] <= 0.0) {
                    continue;
                }

                for (b = a + 1; b < 16; b++) {
                    double fi;
                    double fj;
                    double sep;
                    double midpoint;
                    double score;

                    if (top_power[b] <= 0.0) {
                        continue;
                    }

                    fi = fft_bin_freq(
                            top_bins[a], nfft, (double)sample_rate);
                    fj = fft_bin_freq(
                            top_bins[b], nfft, (double)sample_rate);

                    if (fj < fi) {
                        double t = fi;
                        fi = fj;
                        fj = t;
                    }

                    sep = fj - fi;
                    midpoint = (fi + fj) / 2.0;

                    if (sep >= 34500.0 &&
                            sep <= 37500.0 &&
                            fabs(midpoint) <= 22000.0) {
                        double weaker =
                                top_power[a] < top_power[b] ?
                                        top_power[a] :
                                        top_power[b];

                        score = weaker / background;

                        if (score > row_best_score) {
                            row_best_score = score;
                            row_best_midpoint = midpoint;
                            row_best_sep = sep;
                        }
                    }
                }
            }

            if (row_best_score > 20.0) {
                psk_candidate_t detected;

                detected.offset_s =
                        (double)(row * window_samples) /
                        (double)sample_rate;
                detected.signature_db =
                        10.0 * log10(row_best_score);
                detected.carrier_offset_hz =
                        row_best_midpoint;
                detected.symbol_rate_hint =
                        row_best_sep;

                if (!found ||
                        detected.signature_db >
                                global_best.signature_db) {
                    global_best = detected;
                    found = 1;
                }
            }
        }

        free(buf);
        free(bg);
    }

    if (found) {
        *candidate = global_best;
    }

    return found;
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

    cumulative = malloc((count + 1) * sizeof(*cumulative));
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
