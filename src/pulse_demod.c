/** @file
    Pulse demodulation functions.

    Binary demodulators (PWM/PPM/Manchester/...) using a pulse data structure as input.

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "pulse_demod.h"
#include "bitbuffer.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <limits.h>

static int account_event(r_device *device, bitbuffer_t *bits, char const *demod_name)
{
    // run decoder
    int ret = 0;
    if (device->decode_fn) {
        ret = device->decode_fn(device, bits);
    }

    // statistics accounting
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
        fprintf(stderr, "Decoder \"%s\" gave invalid return value %d: notify maintainer\n", device->name, ret);
        exit(1);
    }

    // Debug printout
    if (!device->decode_fn || (device->verbose && ret > 0) || device->verbose > 1) {
        fprintf(stderr, "%s(): %s\n", demod_name, device->name);
        bitbuffer_print(bits);
    }

    return ret;
}

int pulse_demod_pcm(const pulse_data_t *pulses, r_device *device)
{
    float samples_per_us = pulses->sample_rate / 1.0e6;
    int s_short = device->short_width * samples_per_us;
    int s_long  = device->long_width * samples_per_us;
    int s_reset = device->reset_limit * samples_per_us;
    int s_gap   = device->gap_limit * samples_per_us;
    int s_sync  = device->sync_width * samples_per_us;
    int s_tolerance = device->tolerance * samples_per_us;

    // check for rounding to zero
    if ((device->short_width > 0 && s_short <= 0)
            || (device->long_width > 0 && s_long <= 0)
            || (device->reset_limit > 0 && s_reset <= 0)
            || (device->gap_limit > 0 && s_gap <= 0)
            || (device->sync_width > 0 && s_sync <= 0)
            || (device->tolerance > 0 && s_tolerance <= 0)) {
        fprintf(stderr, "sample rate too low for protocol %u \"%s\"\n", device->protocol_num, device->name);
        return 0;
    }

    // precision reciprocals
    float f_short = device->short_width > 0.0 ? 1.0 / (device->short_width * samples_per_us) : 0;
    float f_long  = device->long_width > 0.0 ? 1.0 / (device->long_width * samples_per_us) : 0;

    int events = 0;
    bitbuffer_t bits = {0};

    int const gap_limit = s_gap ? s_gap : s_reset;
    int const max_zeros = gap_limit / s_long;
    if (s_tolerance <= 0)
        s_tolerance = s_long / 4; // default tolerance is +-25% of a bit period

    // if there is a run of bit-wide toggles (preamble) tune the bit period
    int min_count = s_short == s_long ? 12 : 4;
    int preamble_len = 0;
    // RZ
    for (unsigned n = 0; s_short != s_long && n < pulses->num_pulses; ++n) {
        int swidth = 0;
        int lwidth = 0;
        int count = 0;
        while (n < pulses->num_pulses
                && pulses->pulse[n] >= s_short - s_tolerance
                && pulses->pulse[n] <= s_short + s_tolerance
                && pulses->pulse[n] + pulses->gap[n] >= s_long - s_tolerance
                && pulses->pulse[n] + pulses->gap[n] <= s_long + s_tolerance) {
            swidth += pulses->pulse[n];
            lwidth += pulses->pulse[n] + pulses->gap[n];
            count += 1;
            n++;
        }
        // require at least min_count bits preamble
        if (count >= min_count) {
            f_long  = (float)count / lwidth;
            f_short = (float)count / swidth;
            min_count = count;
            preamble_len = count;
            if (device->verbose > 1) {
                float to_us = 1e6 / pulses->sample_rate;
                fprintf(stderr, "Exact bit width (in us) is %.2f vs %.2f (pulse width %.2f vs %.2f), %d bit preamble\n",
                        to_us / f_long, to_us * s_long,
                        to_us / f_short, to_us * s_short, count);
            }
        }
    }
    // RZ bits within tolerance anywhere
    int rzs_width = 0;
    int rzl_width = 0;
    int rz_count = 0;
    for (unsigned n = 0; preamble_len == 0 && s_short != s_long && n < pulses->num_pulses; ++n) {
        if (pulses->pulse[n] >= s_short - s_tolerance
                && pulses->pulse[n] <= s_short + s_tolerance
                && pulses->pulse[n] + pulses->gap[n] >= s_long - s_tolerance
                && pulses->pulse[n] + pulses->gap[n] <= s_long + s_tolerance) {
            rzs_width += pulses->pulse[n];
            rzl_width += pulses->pulse[n] + pulses->gap[n];
            rz_count += 1;
        }
    }
    // require at least 8 bits measured
    if (rz_count > 8) {
        f_long  = (float)rz_count / rzl_width;
        f_short = (float)rz_count / rzs_width;
        if (device->verbose > 1) {
            float to_us = 1e6 / pulses->sample_rate;
            fprintf(stderr, "Exact bit width (in us) is %.2f vs %.2f (pulse width %.2f vs %.2f), %d bit measured\n",
                    to_us / f_long, to_us * s_long,
                    to_us / f_short, to_us * s_short, rz_count);
        }
    }
    // NRZ
    for (unsigned n = 0; s_short == s_long && n < pulses->num_pulses; ++n) {
        int width = 0;
        int count = 0;
        while (n < pulses->num_pulses
                && (int)(pulses->pulse[n] * f_short + 0.5) == 1
                && (int)(pulses->gap[n] * f_long + 0.5) == 1) {
            width += pulses->pulse[n] + pulses->gap[n];
            count += 2;
            n++;
        }
        // require at least min_count full bits preamble
        if (count >= min_count) {
            f_short = f_long = (float)count / width;
            min_count = count;
            preamble_len = count;
            if (device->verbose > 1) {
                float to_us = 1e6 / pulses->sample_rate;
                fprintf(stderr, "Exact bit width (in us) is %.2f vs %.2f, %d bit preamble\n",
                        to_us / f_short, to_us * s_short, count);
            }
        }
    }
    // NRZ pulse/gap of len 1 or 2 within tolerance anywhere
    int nrz_width = 0;
    int nrz_count = 0;
    for (unsigned n = 0; preamble_len == 0 && s_short == s_long && n < pulses->num_pulses; ++n) {
        if (pulses->pulse[n] >= s_short - s_tolerance
                && pulses->pulse[n] <= s_short + s_tolerance) {
            nrz_width += pulses->pulse[n];
            nrz_count += 1;
        }
        if (pulses->pulse[n] >= 2 * s_short - s_tolerance
                && pulses->pulse[n] <= 2 * s_short + s_tolerance) {
            nrz_width += pulses->pulse[n];
            nrz_count += 2;
        }
        if (pulses->gap[n] >= s_long - s_tolerance
                && pulses->gap[n] <= s_long + s_tolerance) {
            nrz_width += pulses->gap[n];
            nrz_count += 1;
        }
        if (pulses->gap[n] >= 2 * s_long - s_tolerance
                && pulses->gap[n] <= 2 * s_long + s_tolerance) {
            nrz_width += pulses->gap[n];
            nrz_count += 2;
        }
    }
    // require at least 10 bits measured
    if (nrz_count > 20) {
        f_short = f_long = (float)nrz_count / nrz_width;
        if (device->verbose > 1) {
            float to_us = 1e6 / pulses->sample_rate;
            fprintf(stderr, "%s: Exact bit width (in us) is %.2f vs %.2f, %d bit measured\n", device->name,
                    to_us / f_short, to_us * s_short, nrz_count);
        }
    }

    for (unsigned n = 0; n < pulses->num_pulses; ++n) {
        // Determine number of high bit periods for NRZ coding, where bits may not be separated
        int highs = (pulses->pulse[n]) * f_short + 0.5;
        // Determine number of low bit periods in current gap length (rounded)
        // for RZ subtract the nominal bit-gap
        int lows = (pulses->gap[n] + s_short - s_long) * f_long + 0.5;

        // Add run of ones (1 for RZ, many for NRZ)
        for (int i = 0; i < highs; ++i) {
            bitbuffer_add_bit(&bits, 1);
        }
        // Add run of zeros, handle possibly negative "lows" gracefully
        lows = MIN(lows, max_zeros); // Don't overflow at end of message
        for (int i = 0; i < lows; ++i) {
            bitbuffer_add_bit(&bits, 0);
        }

        // Validate data
        if ((s_short != s_long)                                       // Only for RZ coding
                && (abs(pulses->pulse[n] - s_short) > s_tolerance)) { // Pulse must be within tolerance

            // Data is corrupt
            if (device->verbose > 3) {
                fprintf(stderr, "bitbuffer cleared at %u: pulse %d, gap %d, period %d\n",
                        n, pulses->pulse[n], pulses->gap[n],
                        pulses->pulse[n] + pulses->gap[n]);
            }
            bitbuffer_clear(&bits);
        }

        // Check for new packet in multipacket
        else if (pulses->gap[n] > gap_limit && pulses->gap[n] <= s_reset) {
            bitbuffer_add_row(&bits);
        }
        // End of Message?
        if (((n == pulses->num_pulses - 1)                            // No more pulses? (FSK)
                    || (pulses->gap[n] > s_reset))      // Long silence (OOK)
                && (bits.bits_per_row[0] > 0 || bits.num_rows > 1)) { // Only if data has been accumulated

            events += account_event(device, &bits, __func__);
            bitbuffer_clear(&bits);
        }
    } // for
    return events;
}

int pulse_demod_ppm(const pulse_data_t *pulses, r_device *device)
{
    float samples_per_us = pulses->sample_rate / 1.0e6;

    int s_short = device->short_width * samples_per_us;
    int s_long  = device->long_width * samples_per_us;
    int s_reset = device->reset_limit * samples_per_us;
    int s_gap   = device->gap_limit * samples_per_us;
    int s_sync  = device->sync_width * samples_per_us;
    int s_tolerance = device->tolerance * samples_per_us;

    // check for rounding to zero
    if ((device->short_width > 0 && s_short <= 0)
            || (device->long_width > 0 && s_long <= 0)
            || (device->reset_limit > 0 && s_reset <= 0)
            || (device->gap_limit > 0 && s_gap <= 0)
            || (device->sync_width > 0 && s_sync <= 0)
            || (device->tolerance > 0 && s_tolerance <= 0)) {
        fprintf(stderr, "sample rate too low for protocol %u \"%s\"\n", device->protocol_num, device->name);
        return 0;
    }

    int events = 0;
    bitbuffer_t bits = {0};

    // lower and upper bounds (non inclusive)
    int zero_l, zero_u;
    int one_l, one_u;
    int sync_l = 0, sync_u = 0;

    if (s_tolerance > 0) {
        // precise
        zero_l = s_short - s_tolerance;
        zero_u = s_short + s_tolerance;
        one_l  = s_long - s_tolerance;
        one_u  = s_long + s_tolerance;
        if (s_sync > 0) {
            sync_l = s_sync - s_tolerance;
            sync_u = s_sync + s_tolerance;
        }
    }
    else {
        // no sync, short=0, long=1
        zero_l = 0;
        zero_u = (s_short + s_long) / 2 + 1;
        one_l  = zero_u - 1;
        one_u  = s_gap ? s_gap : s_reset;
    }

    for (unsigned n = 0; n < pulses->num_pulses; ++n) {
        if (pulses->gap[n] > zero_l && pulses->gap[n] < zero_u) {
            // Short gap
            bitbuffer_add_bit(&bits, 0);
        }
        else if (pulses->gap[n] > one_l && pulses->gap[n] < one_u) {
            // Long gap
            bitbuffer_add_bit(&bits, 1);
        }
        else if (pulses->gap[n] > sync_l && pulses->gap[n] < sync_u) {
            // Sync gap
            bitbuffer_add_sync(&bits);
        }

        // Check for new packet in multipacket
        else if (pulses->gap[n] < s_reset) {
            bitbuffer_add_row(&bits);
        }
        // End of Message?
        if (((n == pulses->num_pulses - 1)                            // No more pulses? (FSK)
                    || (pulses->gap[n] >= s_reset))     // Long silence (OOK)
                && (bits.bits_per_row[0] > 0 || bits.num_rows > 1)) { // Only if data has been accumulated

            events += account_event(device, &bits, __func__);
            bitbuffer_clear(&bits);
        }
    } // for pulses
    return events;
}

int pulse_demod_pwm(const pulse_data_t *pulses, r_device *device)
{
    float samples_per_us = pulses->sample_rate / 1.0e6;

    int s_short = device->short_width * samples_per_us;
    int s_long  = device->long_width * samples_per_us;
    int s_reset = device->reset_limit * samples_per_us;
    int s_gap   = device->gap_limit * samples_per_us;
    int s_sync  = device->sync_width * samples_per_us;
    int s_tolerance = device->tolerance * samples_per_us;

    // check for rounding to zero
    if ((device->short_width > 0 && s_short <= 0)
            || (device->long_width > 0 && s_long <= 0)
            || (device->reset_limit > 0 && s_reset <= 0)
            || (device->gap_limit > 0 && s_gap <= 0)
            || (device->sync_width > 0 && s_sync <= 0)
            || (device->tolerance > 0 && s_tolerance <= 0)) {
        fprintf(stderr, "sample rate too low for protocol %u \"%s\"\n", device->protocol_num, device->name);
        return 0;
    }

    int events = 0;
    bitbuffer_t bits = {0};

    // lower and upper bounds (non inclusive)
    int one_l, one_u;
    int zero_l, zero_u;
    int sync_l = 0, sync_u = 0;

    if (s_tolerance > 0) {
        // precise
        one_l  = s_short - s_tolerance;
        one_u  = s_short + s_tolerance;
        zero_l = s_long - s_tolerance;
        zero_u = s_long + s_tolerance;
        if (s_sync > 0) {
            sync_l = s_sync - s_tolerance;
            sync_u = s_sync + s_tolerance;
        }
    }
    else if (s_sync <= 0) {
        // no sync, short=1, long=0
        one_l  = 0;
        one_u  = (s_short + s_long) / 2 + 1;
        zero_l = one_u - 1;
        zero_u = INT_MAX;
    }
    else if (s_sync < s_short) {
        // short=sync, middle=1, long=0
        sync_l = 0;
        sync_u = (s_sync + s_short) / 2 + 1;
        one_l  = sync_u - 1;
        one_u  = (s_short + s_long) / 2 + 1;
        zero_l = one_u - 1;
        zero_u = INT_MAX;
    }
    else if (s_sync < s_long) {
        // short=1, middle=sync, long=0
        one_l  = 0;
        one_u  = (s_short + s_sync) / 2 + 1;
        sync_l = one_u - 1;
        sync_u = (s_sync + s_long) / 2 + 1;
        zero_l = sync_u - 1;
        zero_u = INT_MAX;
    }
    else {
        // short=1, middle=0, long=sync
        one_l  = 0;
        one_u  = (s_short + s_long) / 2 + 1;
        zero_l = one_u - 1;
        zero_u = (s_long + s_sync) / 2 + 1;
        sync_l = zero_u - 1;
        sync_u = INT_MAX;
    }

    for (unsigned n = 0; n < pulses->num_pulses; ++n) {
        if (pulses->pulse[n] > one_l && pulses->pulse[n] < one_u) {
            // 'Short' 1 pulse
            bitbuffer_add_bit(&bits, 1);
        }
        else if (pulses->pulse[n] > zero_l && pulses->pulse[n] < zero_u) {
            // 'Long' 0 pulse
            bitbuffer_add_bit(&bits, 0);
        }
        else if (pulses->pulse[n] > sync_l && pulses->pulse[n] < sync_u) {
            // Sync pulse
            bitbuffer_add_sync(&bits);
        }
        else if (pulses->pulse[n] <= one_l) {
            // Ignore spurious short pulses
        }
        else {
            // Pulse outside specified timing
            bitbuffer_add_row(&bits);
        }

        // End of Message?
        if (((n == pulses->num_pulses - 1)                       // No more pulses? (FSK)
                    || (pulses->gap[n] > s_reset)) // Long silence (OOK)
                && (bits.num_rows > 0)) {                        // Only if data has been accumulated
            events += account_event(device, &bits, __func__);
            bitbuffer_clear(&bits);
        }
        else if (s_gap > 0 && pulses->gap[n] > s_gap
                && bits.num_rows > 0 && bits.bits_per_row[bits.num_rows - 1] > 0) {
            // New packet in multipacket
            bitbuffer_add_row(&bits);
        }
    }
    return events;
}

int pulse_demod_manchester_zerobit(const pulse_data_t *pulses, r_device *device)
{
    float samples_per_us = pulses->sample_rate / 1.0e6;

    int s_short = device->short_width * samples_per_us;
    int s_long  = device->long_width * samples_per_us;
    int s_reset = device->reset_limit * samples_per_us;
    int s_gap   = device->gap_limit * samples_per_us;
    int s_sync  = device->sync_width * samples_per_us;
    int s_tolerance = device->tolerance * samples_per_us;

    // check for rounding to zero
    if ((device->short_width > 0 && s_short <= 0)
            || (device->long_width > 0 && s_long <= 0)
            || (device->reset_limit > 0 && s_reset <= 0)
            || (device->gap_limit > 0 && s_gap <= 0)
            || (device->sync_width > 0 && s_sync <= 0)
            || (device->tolerance > 0 && s_tolerance <= 0)) {
        fprintf(stderr, "sample rate too low for protocol %u \"%s\"\n", device->protocol_num, device->name);
        return 0;
    }

    int events = 0;
    int time_since_last = 0;
    bitbuffer_t bits = {0};

    // First rising edge is always counted as a zero (Seems to be hardcoded policy for the Oregon Scientific sensors...)
    bitbuffer_add_bit(&bits, 0);

    for (unsigned n = 0; n < pulses->num_pulses; ++n) {
        // The pulse or gap is too long or too short, thus invalid
        if (s_tolerance > 0
                && (pulses->pulse[n] < s_short - s_tolerance
                || pulses->pulse[n] > s_short * 2 + s_tolerance
                || pulses->gap[n] < s_short - s_tolerance
                || pulses->gap[n] > s_short * 2 + s_tolerance)) {
            if (pulses->pulse[n] > s_short * 1.5
                    && pulses->pulse[n] <= s_short * 2 + s_tolerance) {
                // Long last pulse means with the gap this is a [1]10 transition, add a one
                bitbuffer_add_bit(&bits, 1);
            }
            bitbuffer_add_row(&bits);
            bitbuffer_add_bit(&bits, 0); // Prepare for new message with hardcoded 0
            time_since_last = 0;
        }
        // Falling edge is on end of pulse
        else if (pulses->pulse[n] + time_since_last > (s_short * 1.5)) {
            // Last bit was recorded more than short_width*1.5 samples ago
            // so this pulse start must be a data edge (falling data edge means bit = 1)
            bitbuffer_add_bit(&bits, 1);
            time_since_last = 0;
        }
        else {
            time_since_last += pulses->pulse[n];
        }

        // End of Message?
        if (((n == pulses->num_pulses - 1)                       // No more pulses? (FSK)
                    || (pulses->gap[n] > s_reset)) // Long silence (OOK)
                && (bits.num_rows > 0)) {                        // Only if data has been accumulated
            events += account_event(device, &bits, __func__);
            bitbuffer_clear(&bits);
            bitbuffer_add_bit(&bits, 0); // Prepare for new message with hardcoded 0
            time_since_last = 0;
        }
        // Rising edge is on end of gap
        else if (pulses->gap[n] + time_since_last > (s_short * 1.5)) {
            // Last bit was recorded more than short_width*1.5 samples ago
            // so this pulse end is a data edge (rising data edge means bit = 0)
            bitbuffer_add_bit(&bits, 0);
            time_since_last = 0;
        }
        else {
            time_since_last += pulses->gap[n];
        }
    }
    return events;
}

static inline int pulse_demod_get_symbol(const pulse_data_t *pulses, unsigned int n)
{
    if (n % 2 == 0)
        return pulses->pulse[n / 2];
    else
        return pulses->gap[n / 2];
}

int pulse_demod_dmc(const pulse_data_t *pulses, r_device *device)
{
    float samples_per_us = pulses->sample_rate / 1.0e6;

    int s_short = device->short_width * samples_per_us;
    int s_long  = device->long_width * samples_per_us;
    int s_reset = device->reset_limit * samples_per_us;
    int s_gap   = device->gap_limit * samples_per_us;
    int s_sync  = device->sync_width * samples_per_us;
    int s_tolerance = device->tolerance * samples_per_us;

    // check for rounding to zero
    if ((device->short_width > 0 && s_short <= 0)
            || (device->long_width > 0 && s_long <= 0)
            || (device->reset_limit > 0 && s_reset <= 0)
            || (device->gap_limit > 0 && s_gap <= 0)
            || (device->sync_width > 0 && s_sync <= 0)
            || (device->tolerance > 0 && s_tolerance <= 0)) {
        fprintf(stderr, "sample rate too low for protocol %u \"%s\"\n", device->protocol_num, device->name);
        return 0;
    }

    bitbuffer_t bits = {0};
    int events = 0;

    for (unsigned int n = 0; n < pulses->num_pulses * 2; ++n) {
        int symbol = pulse_demod_get_symbol(pulses, n);

        if (abs(symbol - s_short) < s_tolerance) {
            // Short - 1
            bitbuffer_add_bit(&bits, 1);
            symbol = pulse_demod_get_symbol(pulses, ++n);
            if (abs(symbol - s_short) > s_tolerance) {
                if (symbol >= s_reset - s_tolerance) {
                    // Don't expect another short gap at end of message
                    n--;
                }
                else if (bits.num_rows > 0 && bits.bits_per_row[bits.num_rows - 1] > 0) {
                    bitbuffer_add_row(&bits);
/*
                    fprintf(stderr, "Detected error during pulse_demod_dmc(): %s\n",
                            device->name);
*/
                }
            }
        }
        else if (abs(symbol - s_long) < s_tolerance) {
            // Long - 0
            bitbuffer_add_bit(&bits, 0);
        }
        else if (symbol >= s_reset - s_tolerance
                && bits.num_rows > 0) { // Only if data has been accumulated
            //END message ?
            events += account_event(device, &bits, __func__);
        }
    }

    return events;
}

int pulse_demod_piwm_raw(const pulse_data_t *pulses, r_device *device)
{
    float samples_per_us = pulses->sample_rate / 1.0e6;

    int s_short = device->short_width * samples_per_us;
    int s_long  = device->long_width * samples_per_us;
    int s_reset = device->reset_limit * samples_per_us;
    int s_gap   = device->gap_limit * samples_per_us;
    int s_sync  = device->sync_width * samples_per_us;
    int s_tolerance = device->tolerance * samples_per_us;

    // check for rounding to zero
    if ((device->short_width > 0 && s_short <= 0)
            || (device->long_width > 0 && s_long <= 0)
            || (device->reset_limit > 0 && s_reset <= 0)
            || (device->gap_limit > 0 && s_gap <= 0)
            || (device->sync_width > 0 && s_sync <= 0)
            || (device->tolerance > 0 && s_tolerance <= 0)) {
        fprintf(stderr, "sample rate too low for protocol %u \"%s\"\n", device->protocol_num, device->name);
        return 0;
    }

    // precision reciprocal
    float f_short = device->short_width > 0.0 ? 1.0 / (device->short_width * samples_per_us) : 0;

    int w;

    bitbuffer_t bits = {0};
    int events = 0;

    for (unsigned int n = 0; n < pulses->num_pulses * 2; ++n) {
        int symbol = pulse_demod_get_symbol(pulses, n);
        w = symbol * f_short + 0.5;
        if (symbol > s_long) {
            bitbuffer_add_row(&bits);
        }
        else if (abs(symbol - w * s_short) < s_tolerance) {
            // Add w symbols
            for (; w > 0; --w)
                bitbuffer_add_bit(&bits, 1 - n % 2);
        }
        else if (symbol < s_reset
                && bits.num_rows > 0
                && bits.bits_per_row[bits.num_rows - 1] > 0) {
            bitbuffer_add_row(&bits);
/*
            fprintf(stderr, "Detected error during pulse_demod_piwm_raw(): %s\n",
                    device->name);
*/
        }

        if (((n == pulses->num_pulses * 2 - 1)              // No more pulses? (FSK)
                    || (symbol > s_reset)) // Long silence (OOK)
                && (bits.num_rows > 0)) {                   // Only if data has been accumulated
            //END message ?
            events += account_event(device, &bits, __func__);
        }
    }

    return events;
}

int pulse_demod_piwm_dc(const pulse_data_t *pulses, r_device *device)
{
    float samples_per_us = pulses->sample_rate / 1.0e6;

    int s_short = device->short_width * samples_per_us;
    int s_long  = device->long_width * samples_per_us;
    int s_reset = device->reset_limit * samples_per_us;
    int s_gap   = device->gap_limit * samples_per_us;
    int s_sync  = device->sync_width * samples_per_us;
    int s_tolerance = device->tolerance * samples_per_us;

    // check for rounding to zero
    if ((device->short_width > 0 && s_short <= 0)
            || (device->long_width > 0 && s_long <= 0)
            || (device->reset_limit > 0 && s_reset <= 0)
            || (device->gap_limit > 0 && s_gap <= 0)
            || (device->sync_width > 0 && s_sync <= 0)
            || (device->tolerance > 0 && s_tolerance <= 0)) {
        fprintf(stderr, "sample rate too low for protocol %u \"%s\"\n", device->protocol_num, device->name);
        return 0;
    }

    bitbuffer_t bits = {0};
    int events = 0;

    for (unsigned int n = 0; n < pulses->num_pulses * 2; ++n) {
        int symbol = pulse_demod_get_symbol(pulses, n);
        if (abs(symbol - s_short) < s_tolerance) {
            // Short - 1
            bitbuffer_add_bit(&bits, 1);
        }
        else if (abs(symbol - s_long) < s_tolerance) {
            // Long - 0
            bitbuffer_add_bit(&bits, 0);
        }
        else if (symbol < s_reset
                && bits.num_rows > 0
                && bits.bits_per_row[bits.num_rows - 1] > 0) {
            bitbuffer_add_row(&bits);
/*
            fprintf(stderr, "Detected error during pulse_demod_piwm_dc(): %s\n",
                    device->name);
*/
        }

        if (((n == pulses->num_pulses * 2 - 1)              // No more pulses? (FSK)
                    || (symbol > s_reset)) // Long silence (OOK)
                && (bits.num_rows > 0)) {                   // Only if data has been accumulated
            //END message ?
            events += account_event(device, &bits, __func__);
        }
    }

    return events;
}

int pulse_demod_nrzs(const pulse_data_t *pulses, r_device *device)
{
    float samples_per_us = pulses->sample_rate / 1.0e6;

    int s_short = device->short_width * samples_per_us;
    int s_long  = device->long_width * samples_per_us;
    int s_reset = device->reset_limit * samples_per_us;
    int s_gap   = device->gap_limit * samples_per_us;
    int s_sync  = device->sync_width * samples_per_us;
    int s_tolerance = device->tolerance * samples_per_us;

    // check for rounding to zero
    if ((device->short_width > 0 && s_short <= 0)
            || (device->long_width > 0 && s_long <= 0)
            || (device->reset_limit > 0 && s_reset <= 0)
            || (device->gap_limit > 0 && s_gap <= 0)
            || (device->sync_width > 0 && s_sync <= 0)
            || (device->tolerance > 0 && s_tolerance <= 0)) {
        fprintf(stderr, "sample rate too low for protocol %u \"%s\"\n", device->protocol_num, device->name);
        return 0;
    }

    int events = 0;
    bitbuffer_t bits = {0};
    int limit = s_short;

    for (unsigned n = 0; n < pulses->num_pulses; ++n) {
        if (pulses->pulse[n] > limit) {
            for (int i = 0 ; i < (pulses->pulse[n]/limit) ; i++) {
                bitbuffer_add_bit(&bits, 1);
            }
            bitbuffer_add_bit(&bits, 0);
        } else if (pulses->pulse[n] < limit) {
            bitbuffer_add_bit(&bits, 0);
        }

        if (n == pulses->num_pulses - 1
                    || pulses->gap[n] >= s_reset) {

            events += account_event(device, &bits, __func__);
        }
    }

    return events;
}


/*
 * Oregon Scientific V1 Protocol
 * Starts with a clean preamble of 12 pulses with
 * consistent timing followed by an out of time Sync pulse.
 * Data then follows with manchester encoding, but
 * care must be taken with the gap after the sync pulse since it
 * is outside of the normal clocking.  Because of this a data stream
 * beginning with a 0 will have data in this gap.
 * This code looks at pulse and gap width and clocks bits
 * in from this.  Since this is manchester encoded every other
 * bit is discarded.
 */

int pulse_demod_osv1(const pulse_data_t *pulses, r_device *device)
{
    float samples_per_us = pulses->sample_rate / 1.0e6;

    int s_short = device->short_width * samples_per_us;
    int s_long  = device->long_width * samples_per_us;
    int s_reset = device->reset_limit * samples_per_us;
    int s_gap   = device->gap_limit * samples_per_us;
    int s_sync  = device->sync_width * samples_per_us;
    int s_tolerance = device->tolerance * samples_per_us;

    // check for rounding to zero
    if ((device->short_width > 0 && s_short <= 0)
            || (device->long_width > 0 && s_long <= 0)
            || (device->reset_limit > 0 && s_reset <= 0)
            || (device->gap_limit > 0 && s_gap <= 0)
            || (device->sync_width > 0 && s_sync <= 0)
            || (device->tolerance > 0 && s_tolerance <= 0)) {
        fprintf(stderr, "sample rate too low for protocol %u \"%s\"\n", device->protocol_num, device->name);
        return 0;
    }

    unsigned int n;
    int preamble = 0;
    int events = 0;
    int manbit = 0;
    bitbuffer_t bits = {0};
    int halfbit_min = s_short / 2;
    int halfbit_max = s_short * 3 / 2;
    int sync_min = 2 * halfbit_max;

    /* preamble */
    for (n = 0; n < pulses->num_pulses; ++n) {
        if (pulses->pulse[n] > halfbit_min && pulses->gap[n] > halfbit_min) {
            preamble++;
            if (pulses->gap[n] > halfbit_max)
                break;
        }
        else
            return events;
    }
    if (preamble != 12) {
        if (device->verbose)
            fprintf(stderr, "preamble %d  %d %d\n", preamble, pulses->pulse[0], pulses->gap[0]);
        return events;
    }

    /* sync */
    ++n;
    if (pulses->pulse[n] < sync_min || pulses->gap[n] < sync_min) {
        return events;
    }

    /* data bits - manchester encoding */

    /* sync gap could be part of data when the first bit is 0 */
    if (pulses->gap[n] > pulses->pulse[n]) {
        manbit ^= 1;
        if (manbit)
            bitbuffer_add_bit(&bits, 0);
    }

    /* remaining data bits */
    for (n++; n < pulses->num_pulses; ++n) {
        manbit ^= 1;
        if (manbit)
            bitbuffer_add_bit(&bits, 1);
        if (pulses->pulse[n] > halfbit_max) {
            manbit ^= 1;
            if (manbit)
                bitbuffer_add_bit(&bits, 1);
        }
        if ((n == pulses->num_pulses - 1
                    || pulses->gap[n] > s_reset)
                && (bits.num_rows > 0)) { // Only if data has been accumulated
            //END message ?
            events += account_event(device, &bits, __func__);
            return events;
        }
        manbit ^= 1;
        if (manbit)
            bitbuffer_add_bit(&bits, 0);
        if (pulses->gap[n] > halfbit_max) {
            manbit ^= 1;
            if (manbit)
                bitbuffer_add_bit(&bits, 0);
        }
    }
    return events;
}

int pulse_demod_string(const char *code, r_device *device)
{
    int events = 0;
    bitbuffer_t bits = {0};

    bitbuffer_parse(&bits, code);

    events += account_event(device, &bits, __func__);

    return events;
}
