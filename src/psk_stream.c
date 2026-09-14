/** @file
    Streaming raw-IQ support for PSK demodulation.

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "psk_stream.h"

#include "bitbuffer.h"
#include "decoder_util.h"
#include "logger.h"
#include "psk_demod.h"
#include "r_device.h"
#include "r_private.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * The PSK detector operates on overlapping rolling windows so a transmission
 * crossing an SDR input-buffer boundary is still presented to the detector
 * as one continuous IQ region.
 *
 * The existing candidate decoder extracts +/-12 ms around the detected
 * signature. A 64 ms window with 24 ms overlap therefore provides ample
 * context around a TX63 transmission while keeping the FFT work bounded.
 */
#define PSK_STREAM_WINDOW_MS 64
#define PSK_STREAM_OVERLAP_MS 24

/*
 * The same RF burst may be present in two adjacent overlapping windows.
 * Candidate positions within 12 ms of the last successfully decoded burst
 * are treated as the same burst.
 */
#define PSK_DUPLICATE_MS 12

/*
 * psk_demod_candidate() requires at least 15 ms of candidate IQ. At EOF,
 * attempt a final partial window only when at least 20 ms remains.
 */
#define PSK_FLUSH_MIN_MS 20

static int account_psk_event(
        r_device *device,
        bitbuffer_t *bits)
{
    int ret = 0;
    unsigned max_bits = 0;

    if (device->decode_fn) {
        ret = device->decode_fn(device, bits);
    }

    device->decode_events += 1;

    if (ret > 0) {
        device->decode_ok += 1;
        device->decode_messages += ret;
    }
    else if (ret >= DECODE_FAIL_SANITY) {
        device->decode_fails[-ret] += 1;
        ret = 0;
    }
    else {
        print_logf(
                LOG_ERROR,
                "PSK",
                "Decoder \"%s\" gave invalid return value %d: notify maintainer",
                device->name,
                ret);
        exit(1);
    }

    for (int row = 0; row < bits->num_rows; ++row) {
        if (bits->bits_per_row[row] > max_bits) {
            max_bits = bits->bits_per_row[row];
        }
    }

    if (!device->decode_fn
            || (device->verbose && ret > 0)
            || (device->verbose > 1 && max_bits > 16)
            || (device->verbose > 2)) {
        decoder_log_bitbuffer(
                device,
                ret > 0 ? 1 : 2,
                "PSK",
                bits,
                device->name);
    }

    bitbuffer_clear(bits);

    return ret;
}

static int candidate_is_duplicate(
        struct dm_state const *demod,
        uint64_t candidate_pos)
{
    uint64_t duplicate_samples;
    uint64_t delta;

    if (!demod->psk_have_last_event) {
        return 0;
    }

    duplicate_samples =
            ((uint64_t)demod->samp_rate * PSK_DUPLICATE_MS) /
            1000;

    if (candidate_pos >= demod->psk_last_event_pos) {
        delta = candidate_pos - demod->psk_last_event_pos;
    }
    else {
        delta = demod->psk_last_event_pos - candidate_pos;
    }

    return delta <= duplicate_samples;
}

static void set_candidate_position(
        struct dm_state *demod,
        uint64_t candidate_pos,
        uint64_t current_end_pos)
{
    uint64_t ago;

    if (candidate_pos >= current_end_pos) {
        demod->pulse_data.start_ago = 0;
        demod->pulse_data.end_ago   = 0;
        return;
    }

    ago = current_end_pos - candidate_pos;

    if (ago > UINT_MAX) {
        ago = UINT_MAX;
    }

    /*
     * The candidate detector reports the center of its strongest 4 ms
     * signature window rather than exact packet edges. For PSK this position
     * is sufficient for rtl_433 timestamp bookkeeping.
     */
    demod->pulse_data.start_ago = (unsigned)ago;
    demod->pulse_data.end_ago   = (unsigned)ago;
}

static int run_psk_window(
        r_cfg_t *cfg,
        uint8_t const *iq_buf,
        size_t sample_count,
        uint64_t window_start_pos,
        uint64_t current_end_pos)
{
    struct dm_state *demod = cfg->demod;
    psk_candidate_t candidate;
    uint64_t candidate_pos;
    unsigned next_priority;
    int events = 0;

    if (!iq_buf || sample_count == 0) {
        return 0;
    }

    if (!psk_find_candidate(
                iq_buf,
                sample_count,
                demod->sample_size,
                demod->samp_rate,
                &candidate)) {
        return 0;
    }

    candidate_pos =
            window_start_pos +
            (uint64_t)llround(
                    candidate.offset_s *
                    demod->samp_rate);

    if (candidate_is_duplicate(demod, candidate_pos)) {
        return 0;
    }

    /*
     * A PSK candidate corresponds to the same concept as an OOK/FSK detected
     * package. Count it as a PSK frame whether or not a protocol validates it.
     */
    demod->total_frames_psk += 1;
    demod->frames_psk += 1;

    set_candidate_position(
            demod,
            candidate_pos,
            current_end_pos);

    /*
     * Follow the normal rtl_433 priority behavior: try the lowest configured
     * PSK decoder priority first and stop advancing to later priorities after
     * an event has been produced.
     */
    next_priority = 0;

    for (unsigned priority = 0;
            !events && priority < UINT_MAX;
            priority = next_priority) {
        next_priority = UINT_MAX;

        for (void **iter = demod->r_devs.elems;
                iter && *iter;
                ++iter) {
            r_device *r_dev = *iter;
            bitbuffer_t bits = {0};

            if (r_dev->modulation != PSK_PULSE_DBPSK) {
                continue;
            }

            if (r_dev->priority > priority
                    && r_dev->priority < next_priority) {
                next_priority = r_dev->priority;
            }

            if (r_dev->priority != priority) {
                continue;
            }

            if (!r_dev->validate_fn || !r_dev->decode_fn) {
                continue;
            }

            if (!psk_demod_candidate(
                        iq_buf,
                        sample_count,
                        demod->sample_size,
                        demod->samp_rate,
                        &candidate,
                        r_dev->validate_fn,
                        r_dev->decode_ctx,
                        &bits)) {
                continue;
            }

            events += account_psk_event(
                    r_dev,
                    &bits);
        }
    }

    if (events > 0) {
        demod->psk_last_event_pos  = candidate_pos;
        demod->psk_have_last_event = 1;

        demod->total_frames_events += 1;
        demod->frames_events += 1;
    }

    return events;
}

void psk_stream_reset(r_cfg_t *cfg)
{
    struct dm_state *demod;

    if (!cfg || !cfg->demod) {
        return;
    }

    demod = cfg->demod;

    demod->psk_iq_len          = 0;
    demod->psk_iq_start_pos    = demod->input_pos;
    demod->psk_last_event_pos  = 0;
    demod->psk_have_last_event = 0;
}

int psk_stream_push(
        r_cfg_t *cfg,
        uint8_t const *iq_buf,
        size_t sample_count)
{
    struct dm_state *demod;
    size_t window_samples;
    size_t overlap_samples;
    size_t hop_samples;
    size_t window_bytes;
    size_t hop_bytes;
    size_t consumed = 0;
    uint64_t current_end_pos;
    int events = 0;

    if (!cfg || !cfg->demod) {
        return 0;
    }

    demod = cfg->demod;

    if (!demod->enable_PSK_demod) {
        return 0;
    }

    /*
     * CU8 is the only raw-IQ representation currently validated by the PSK
     * detector. Do not reinterpret CS16 samples as CU8.
     */
    if (demod->sample_size != 2) {
        return 0;
    }

    if (demod->samp_rate == 0) {
        return 0;
    }

    window_samples =
            ((size_t)demod->samp_rate *
                    PSK_STREAM_WINDOW_MS) /
            1000;

    overlap_samples =
            ((size_t)demod->samp_rate *
                    PSK_STREAM_OVERLAP_MS) /
            1000;

    if (window_samples == 0
            || overlap_samples >= window_samples) {
        return 0;
    }

    window_bytes =
            window_samples *
            (size_t)demod->sample_size;

    if (window_bytes > sizeof(demod->psk_iq_buf)) {
        print_logf(
                LOG_WARNING,
                "PSK",
                "Sample rate %u is too high for the PSK streaming buffer",
                demod->samp_rate);
        return 0;
    }

    hop_samples =
            window_samples -
            overlap_samples;

    hop_bytes =
            hop_samples *
            (size_t)demod->sample_size;

    /*
     * At normal input calls input_pos points to the first sample in iq_buf.
     * At EOF input_pos already points immediately after the final input block.
     */
    current_end_pos =
            demod->input_pos +
            sample_count;

    /*
     * EOF flush: process the remaining overlap/partial window once.
     */
    if (!iq_buf || sample_count == 0) {
        size_t buffered_samples =
                demod->psk_iq_len /
                (size_t)demod->sample_size;

        size_t flush_min_samples =
                ((size_t)demod->samp_rate *
                        PSK_FLUSH_MIN_MS) /
                1000;

        if (buffered_samples >= flush_min_samples) {
            events += run_psk_window(
                    cfg,
                    demod->psk_iq_buf,
                    buffered_samples,
                    demod->psk_iq_start_pos,
                    current_end_pos);
        }

        demod->psk_iq_len = 0;

        return events;
    }

    while (consumed < sample_count) {
        size_t buffered_samples =
                demod->psk_iq_len /
                (size_t)demod->sample_size;

        size_t needed_samples =
                window_samples -
                buffered_samples;

        size_t remaining_samples =
                sample_count -
                consumed;

        size_t copy_samples =
                remaining_samples < needed_samples
                        ? remaining_samples
                        : needed_samples;

        size_t copy_bytes =
                copy_samples *
                (size_t)demod->sample_size;

        if (demod->psk_iq_len == 0) {
            demod->psk_iq_start_pos =
                    demod->input_pos +
                    consumed;
        }

        memcpy(
                demod->psk_iq_buf +
                        demod->psk_iq_len,
                iq_buf +
                        consumed *
                                (size_t)demod->sample_size,
                copy_bytes);

        demod->psk_iq_len += copy_bytes;
        consumed += copy_samples;

        if (demod->psk_iq_len < window_bytes) {
            continue;
        }

        events += run_psk_window(
                cfg,
                demod->psk_iq_buf,
                window_samples,
                demod->psk_iq_start_pos,
                current_end_pos);

        memmove(
                demod->psk_iq_buf,
                demod->psk_iq_buf + hop_bytes,
                demod->psk_iq_len - hop_bytes);

        demod->psk_iq_len -= hop_bytes;
        demod->psk_iq_start_pos += hop_samples;
    }

    return events;
}