/** @file
    Pulse detection functions.

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "pulse_detect.h"
#include "rfraw.h"
#include "pulse_demod.h"
#include "pulse_detect_fsk.h"
#include "baseband.h"
#include "util.h"
#include "r_device.h"
#include "r_util.h"
#include "fatal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pulse_data_clear(pulse_data_t *data)
{
    *data = (pulse_data_t const){0};
}

void pulse_data_print(pulse_data_t const *data)
{
    fprintf(stderr, "Pulse data: %u pulses\n", data->num_pulses);
    for (unsigned n = 0; n < data->num_pulses; ++n) {
        fprintf(stderr, "[%3u] Pulse: %4d, Gap: %4d, Period: %4d\n", n, data->pulse[n], data->gap[n], data->pulse[n] + data->gap[n]);
    }
}

static void *bounded_memset(void *b, int c, int64_t size, int64_t offset, int64_t len)
{
    if (offset < 0) {
        len += offset; // reduce len by negative offset
        offset = 0;
    }
    if (offset + len > size) {
        len = size - offset; // clip excessive len
    }
    if (len > 0)
        memset((char *)b + offset, c, (size_t)len);
    return b;
}

void pulse_data_dump_raw(uint8_t *buf, unsigned len, uint64_t buf_offset, pulse_data_t const *data, uint8_t bits)
{
    int64_t pos = data->offset - buf_offset;
    for (unsigned n = 0; n < data->num_pulses; ++n) {
        bounded_memset(buf, 0x01 | bits, len, pos, data->pulse[n]);
        pos += data->pulse[n];
        bounded_memset(buf, 0x01, len, pos, data->gap[n]);
        pos += data->gap[n];
    }
}

static inline void chk_ret(int ret)
{
    if (ret < 0) {
        perror("File output error");
        exit(1);
    }
}

void pulse_data_print_vcd_header(FILE *file, uint32_t sample_rate)
{
    char time_str[LOCAL_TIME_BUFLEN];
    char *timescale;
    if (sample_rate <= 500000)
        timescale = "1 us";
    else
        timescale = "100 ns";
    chk_ret(fprintf(file, "$date %s $end\n", format_time_str(time_str, NULL, 0, 0)));
    chk_ret(fprintf(file, "$version rtl_433 0.1.0 $end\n"));
    chk_ret(fprintf(file, "$comment Acquisition at %s Hz $end\n", nice_freq(sample_rate)));
    chk_ret(fprintf(file, "$timescale %s $end\n", timescale));
    chk_ret(fprintf(file, "$scope module rtl_433 $end\n"));
    chk_ret(fprintf(file, "$var wire 1 / FRAME $end\n"));
    chk_ret(fprintf(file, "$var wire 1 ' AM $end\n"));
    chk_ret(fprintf(file, "$var wire 1 \" FM $end\n"));
    chk_ret(fprintf(file, "$upscope $end\n"));
    chk_ret(fprintf(file, "$enddefinitions $end\n"));
    chk_ret(fprintf(file, "#0 0/ 0' 0\"\n"));
}

void pulse_data_print_vcd(FILE *file, pulse_data_t const *data, int ch_id)
{
    float scale;
    if (data->sample_rate <= 500000)
        scale = 1000000 / data->sample_rate; // unit: 1 us
    else
        scale = 10000000 / data->sample_rate; // unit: 100 ns
    uint64_t pos = data->offset;
    for (unsigned n = 0; n < data->num_pulses; ++n) {
        if (n == 0)
            chk_ret(fprintf(file, "#%.f 1/ 1%c\n", pos * scale, ch_id));
        else
            chk_ret(fprintf(file, "#%.f 1%c\n", pos * scale, ch_id));
        pos += data->pulse[n];
        chk_ret(fprintf(file, "#%.f 0%c\n", pos * scale, ch_id));
        pos += data->gap[n];
    }
    if (data->num_pulses > 0)
        chk_ret(fprintf(file, "#%.f 0/\n", pos * scale));
}

void pulse_data_load(FILE *file, pulse_data_t *data, uint32_t sample_rate)
{
    char s[1024];
    int i    = 0;
    int size = sizeof(data->pulse) / sizeof(*data->pulse);

    pulse_data_clear(data);
    data->sample_rate = sample_rate;
    double to_sample = sample_rate / 1e6;
    // read line-by-line
    while (i < size && fgets(s, sizeof(s), file)) {
        // TODO: we should parse sample rate and timescale
        if (!strncmp(s, ";freq1", 6)) {
            data->freq1_hz = strtol(s + 6, NULL, 10);
        }
        if (!strncmp(s, ";freq2", 6)) {
            data->freq2_hz = strtol(s + 6, NULL, 10);
        }
        if (*s == ';') {
            if (i) {
                break; // end or next header found
            }
            else {
                continue; // still reading a header
            }
        }
        if (rfraw_check(s)) {
            rfraw_parse(data, s);
            i = data->num_pulses;
            continue;
        }
        // parse two ints.
        char *p = s;
        char *endptr;
        long mark  = strtol(p, &endptr, 10);
        p          = endptr + 1;
        long space = strtol(p, &endptr, 10);
        //fprintf(stderr, "read: mark %ld space %ld\n", mark, space);
        data->pulse[i] = (int)(to_sample * mark);
        data->gap[i++] = (int)(to_sample * space);
    }
    //fprintf(stderr, "read %d pulses\n", i);
    data->num_pulses = i;
}

void pulse_data_print_pulse_header(FILE *file)
{
    char time_str[LOCAL_TIME_BUFLEN];

    chk_ret(fprintf(file, ";pulse data\n"));
    chk_ret(fprintf(file, ";version 1\n"));
    chk_ret(fprintf(file, ";timescale 1us\n"));
    //chk_ret(fprintf(file, ";samplerate %u\n", data->sample_rate));
    chk_ret(fprintf(file, ";created %s\n", format_time_str(time_str, NULL, 1, 0)));
}

void pulse_data_dump(FILE *file, pulse_data_t *data)
{
    if (data->fsk_f2_est) {
        chk_ret(fprintf(file, ";fsk %u pulses\n", data->num_pulses));
        chk_ret(fprintf(file, ";freq1 %.0f\n", data->freq1_hz));
        chk_ret(fprintf(file, ";freq2 %.0f\n", data->freq2_hz));
    }
    else {
        chk_ret(fprintf(file, ";ook %u pulses\n", data->num_pulses));
        chk_ret(fprintf(file, ";freq1 %.0f\n", data->freq1_hz));
    }
    chk_ret(fprintf(file, ";samplerate %u Hz\n", data->sample_rate));
    chk_ret(fprintf(file, ";rssi %.1f dB\n", data->rssi_db));
    chk_ret(fprintf(file, ";snr %.1f dB\n", data->snr_db));
    chk_ret(fprintf(file, ";noise %.1f dB\n", data->noise_db));

    double to_us = 1e6 / data->sample_rate;
    for (unsigned i = 0; i < data->num_pulses; ++i) {
        chk_ret(fprintf(file, "%.0f %.0f\n", data->pulse[i] * to_us, data->gap[i] * to_us));
    }
    chk_ret(fprintf(file, ";end\n"));
}

// OOK adaptive level estimator constants
#define OOK_MAX_HIGH_LEVEL  DB_TO_AMP(0)   // Maximum estimate for high level (-0 dB)
#define OOK_MAX_LOW_LEVEL   DB_TO_AMP(-15) // Maximum estimate for low level
#define OOK_EST_HIGH_RATIO  64          // Constant for slowness of OOK high level estimator
#define OOK_EST_LOW_RATIO   1024        // Constant for slowness of OOK low level (noise) estimator (very slow)

/// Internal state data for pulse_pulse_package()
struct pulse_detect {
    int use_mag_est;          ///< Wether the envelope data is an amplitude or magnitude.
    int ook_fixed_high_level; ///< Manual detection level override, 0 = auto.
    int ook_min_high_level;   ///< Minimum estimate of high level (-12 dB: 1000 amp, 4000 mag).
    int ook_high_low_ratio;   ///< Default ratio between high and low (noise) level (9 dB: x8 amp, 11 dB: x3.6 mag).

    enum {
        PD_OOK_STATE_IDLE      = 0,
        PD_OOK_STATE_PULSE     = 1,
        PD_OOK_STATE_GAP_START = 2,
        PD_OOK_STATE_GAP       = 3
    } ook_state;
    int pulse_length; ///< Counter for internal pulse detection
    int max_pulse;    ///< Size of biggest pulse detected

    int data_counter;    ///< Counter for how much of data chunk is processed
    int lead_in_counter; ///< Counter for allowing initial noise estimate to settle

    int ook_low_estimate;  ///< Estimate for the OOK low level (base noise level) in the envelope data
    int ook_high_estimate; ///< Estimate for the OOK high level

    int verbosity; ///< Debug output verbosity, 0=None, 1=Levels, 2=Histograms

    pulse_FSK_state_t FSK_state;
};

pulse_detect_t *pulse_detect_create()
{
    pulse_detect_t *pulse_detect = calloc(1, sizeof(pulse_detect_t));
    if (!pulse_detect) {
        WARN_CALLOC("pulse_detect_create()");
        return NULL;
    }

    pulse_detect_set_levels(pulse_detect, 0, 0.0, -12.1442, 9.0, 0);

    return pulse_detect;
}

void pulse_detect_free(pulse_detect_t *pulse_detect)
{
    free(pulse_detect);
}

void pulse_detect_set_levels(pulse_detect_t *pulse_detect, int use_mag_est, float fixed_high_level, float min_high_level, float high_low_ratio, int verbosity)
{
    pulse_detect->use_mag_est = use_mag_est;
    if (use_mag_est) {
        pulse_detect->ook_fixed_high_level = fixed_high_level < 0.0 ? DB_TO_MAG(fixed_high_level) : 0;
        pulse_detect->ook_min_high_level   = DB_TO_MAG(min_high_level);
        pulse_detect->ook_high_low_ratio = DB_TO_MAG_F(high_low_ratio);
    }
    else { // amp est
        pulse_detect->ook_fixed_high_level = fixed_high_level < 0.0 ? DB_TO_AMP(fixed_high_level) : 0;
        pulse_detect->ook_min_high_level   = DB_TO_AMP(min_high_level);
        pulse_detect->ook_high_low_ratio = DB_TO_AMP_F(high_low_ratio);
    }
    pulse_detect->verbosity = verbosity;

    //fprintf(stderr, "fixed_high_level %.1f (%d), min_high_level %.1f (%d), high_low_ratio %.1f (%d)\n",
    //        fixed_high_level, pulse_detect->ook_fixed_high_level,
    //        min_high_level, pulse_detect->ook_min_high_level,
    //        high_low_ratio, pulse_detect->ook_high_low_ratio);
}

/// convert amplitude (16384 FS) to attenuation in (integer) dB, offset by 3.
static inline int amp_to_att(int a)
{
    if (a > 32690) return 0;  // = 10^(( 3 + 42.1442) / 10)
    if (a > 25967) return 1;  // = 10^(( 2 + 42.1442) / 10)
    if (a > 20626) return 2;  // = 10^(( 1 + 42.1442) / 10)
    if (a > 16383) return 3;  // = 10^(( 0 + 42.1442) / 10)
    if (a > 13014) return 4;  // = 10^((-1 + 42.1442) / 10)
    if (a > 10338) return 5;  // = 10^((-2 + 42.1442) / 10)
    if (a >  8211) return 6;  // = 10^((-3 + 42.1442) / 10)
    if (a >  6523) return 7;  // = 10^((-4 + 42.1442) / 10)
    if (a >  5181) return 8;  // = 10^((-5 + 42.1442) / 10)
    if (a >  4115) return 9;  // = 10^((-6 + 42.1442) / 10)
    if (a >  3269) return 10; // = 10^((-7 + 42.1442) / 10)
    if (a >  2597) return 11; // = 10^((-8 + 42.1442) / 10)
    if (a >  2063) return 12; // = 10^((-9 + 42.1442) / 10)
    if (a >  1638) return 13; // = 10^((-10 + 42.1442) / 10)
    if (a >  1301) return 14; // = 10^((-11 + 42.1442) / 10)
    if (a >  1034) return 15; // = 10^((-12 + 42.1442) / 10)
    if (a >   821) return 16; // = 10^((-13 + 42.1442) / 10)
    if (a >   652) return 17; // = 10^((-14 + 42.1442) / 10)
    if (a >   518) return 18; // = 10^((-15 + 42.1442) / 10)
    if (a >   412) return 19; // = 10^((-16 + 42.1442) / 10)
    if (a >   327) return 20; // = 10^((-17 + 42.1442) / 10)
    if (a >   260) return 21; // = 10^((-18 + 42.1442) / 10)
    if (a >   206) return 22; // = 10^((-19 + 42.1442) / 10)
    if (a >   164) return 23; // = 10^((-20 + 42.1442) / 10)
    if (a >   130) return 24; // = 10^((-21 + 42.1442) / 10)
    if (a >   103) return 25; // = 10^((-22 + 42.1442) / 10)
    if (a >    82) return 26; // = 10^((-23 + 42.1442) / 10)
    if (a >    65) return 27; // = 10^((-24 + 42.1442) / 10)
    if (a >    52) return 28; // = 10^((-25 + 42.1442) / 10)
    if (a >    41) return 29; // = 10^((-26 + 42.1442) / 10)
    if (a >    33) return 30; // = 10^((-27 + 42.1442) / 10)
    if (a >    26) return 31; // = 10^((-28 + 42.1442) / 10)
    if (a >    21) return 32; // = 10^((-29 + 42.1442) / 10)
    if (a >    16) return 33; // = 10^((-30 + 42.1442) / 10)
    if (a >    13) return 34; // = 10^((-31 + 42.1442) / 10)
    if (a >    10) return 35; // = 10^((-32 + 42.1442) / 10)
    return 36;
}
/// convert magnitude (16384 FS) to attenuation in (integer) dB, offset by 3.
static inline int mag_to_att(int m)
{
    if (m > 23143) return 0;  // = 10^(( 3 + 84.2884) / 20)
    if (m > 20626) return 1;  // = 10^(( 2 + 84.2884) / 20)
    if (m > 18383) return 2;  // = 10^(( 1 + 84.2884) / 20)
    if (m > 16383) return 3;  // = 10^(( 0 + 84.2884) / 20)
    if (m > 14602) return 4;  // = 10^((-1 + 84.2884) / 20)
    if (m > 13014) return 5;  // = 10^((-2 + 84.2884) / 20)
    if (m > 11599) return 6;  // = 10^((-3 + 84.2884) / 20)
    if (m > 10338) return 7;  // = 10^((-4 + 84.2884) / 20)
    if (m >  9213) return 8;  // = 10^((-5 + 84.2884) / 20)
    if (m >  8211) return 9;  // = 10^((-6 + 84.2884) / 20)
    if (m >  7318) return 10; // = 10^((-7 + 84.2884) / 20)
    if (m >  6523) return 11; // = 10^((-8 + 84.2884) / 20)
    if (m >  5813) return 12; // = 10^((-9 + 84.2884) / 20)
    if (m >  5181) return 13; // = 10^((-10 + 84.2884) / 20)
    if (m >  4618) return 14; // = 10^((-11 + 84.2884) / 20)
    if (m >  4115) return 15; // = 10^((-12 + 84.2884) / 20)
    if (m >  3668) return 16; // = 10^((-13 + 84.2884) / 20)
    if (m >  3269) return 17; // = 10^((-14 + 84.2884) / 20)
    if (m >  2914) return 18; // = 10^((-15 + 84.2884) / 20)
    if (m >  2597) return 19; // = 10^((-16 + 84.2884) / 20)
    if (m >  2314) return 20; // = 10^((-17 + 84.2884) / 20)
    if (m >  2063) return 21; // = 10^((-18 + 84.2884) / 20)
    if (m >  1838) return 22; // = 10^((-19 + 84.2884) / 20)
    if (m >  1638) return 23; // = 10^((-20 + 84.2884) / 20)
    if (m >  1460) return 24; // = 10^((-21 + 84.2884) / 20)
    if (m >  1301) return 25; // = 10^((-22 + 84.2884) / 20)
    if (m >  1160) return 26; // = 10^((-23 + 84.2884) / 20)
    if (m >  1034) return 27; // = 10^((-24 + 84.2884) / 20)
    if (m >   921) return 28; // = 10^((-25 + 84.2884) / 20)
    if (m >   821) return 29; // = 10^((-26 + 84.2884) / 20)
    if (m >   732) return 30; // = 10^((-27 + 84.2884) / 20)
    if (m >   652) return 31; // = 10^((-28 + 84.2884) / 20)
    if (m >   581) return 32; // = 10^((-29 + 84.2884) / 20)
    if (m >   518) return 33; // = 10^((-30 + 84.2884) / 20)
    if (m >   462) return 34; // = 10^((-31 + 84.2884) / 20)
    if (m >   412) return 35; // = 10^((-32 + 84.2884) / 20)
    return 36;
}
/// print a simple attenuation histogram.
static void print_att_hist(char const *s, int att_hist[])
{
    fprintf(stderr, "\n%s\n", s);
    for (int i = 0; i < 37; ++i)
        fprintf(stderr, ">%3d dB: %5d smps\n", 3 - i, att_hist[i]);
}

/// Demodulate On/Off Keying (OOK) and Frequency Shift Keying (FSK) from an envelope signal
int pulse_detect_package(pulse_detect_t *pulse_detect, int16_t const *envelope_data, int16_t const *fm_data, int len, uint32_t samp_rate, uint64_t sample_offset, pulse_data_t *pulses, pulse_data_t *fsk_pulses, unsigned fpdm)
{
    int att_hist[37] = {0};
    int const samples_per_ms = samp_rate / 1000;
    pulse_detect_t *s = pulse_detect;
    s->ook_high_estimate = MAX(s->ook_high_estimate, pulse_detect->ook_min_high_level);    // Be sure to set initial minimum level

    if (s->data_counter == 0) {
        // age the pulse_data if this is a fresh buffer
        pulses->start_ago += len;
        fsk_pulses->start_ago += len;
    }

    // Process all new samples
    while (s->data_counter < len) {
        // Calculate OOK detection threshold and hysteresis
        int16_t const am_n    = envelope_data[s->data_counter];
        if (pulse_detect->verbosity) {
            int att = pulse_detect->use_mag_est ? mag_to_att(am_n) : amp_to_att(am_n);
            att_hist[att]++;
        }
        int16_t ook_threshold = (s->ook_low_estimate + s->ook_high_estimate) / 2;
        if (pulse_detect->ook_fixed_high_level != 0)
            ook_threshold = pulse_detect->ook_fixed_high_level; // Manual override
        int16_t const ook_hysteresis = ook_threshold / 8; // +-12%

        // OOK State machine
        switch (s->ook_state) {
            case PD_OOK_STATE_IDLE:
                if (am_n > (ook_threshold + ook_hysteresis)    // Above threshold?
                        && s->lead_in_counter > OOK_EST_LOW_RATIO) { // Lead in counter to stabilize noise estimate
                    // Initialize all data
                    pulse_data_clear(pulses);
                    pulse_data_clear(fsk_pulses);
                    pulses->sample_rate = samp_rate;
                    fsk_pulses->sample_rate = samp_rate;
                    pulses->offset = sample_offset + s->data_counter;
                    fsk_pulses->offset = sample_offset + s->data_counter;
                    pulses->start_ago = len - s->data_counter;
                    fsk_pulses->start_ago = len - s->data_counter;
                    s->pulse_length = 0;
                    s->max_pulse = 0;
                    s->FSK_state = (pulse_FSK_state_t){0};
                    s->FSK_state.var_test_max = INT16_MIN;
                    s->FSK_state.var_test_min = INT16_MAX;
                    s->FSK_state.skip_samples = 40;
                    s->ook_state = PD_OOK_STATE_PULSE;
                }
                else {    // We are still idle..
                    // Estimate low (noise) level
                    int const ook_low_delta = am_n - s->ook_low_estimate;
                    s->ook_low_estimate += ook_low_delta / OOK_EST_LOW_RATIO;
                    s->ook_low_estimate += ((ook_low_delta > 0) ? 1 : -1);    // Hack to compensate for lack of fixed-point scaling
                    // Calculate default OOK high level estimate
                    s->ook_high_estimate = pulse_detect->ook_high_low_ratio * s->ook_low_estimate; // Default is a ratio of low level
                    s->ook_high_estimate = MAX(s->ook_high_estimate, pulse_detect->ook_min_high_level);
                    s->ook_high_estimate = MIN(s->ook_high_estimate, OOK_MAX_HIGH_LEVEL);
                    if (s->lead_in_counter <= OOK_EST_LOW_RATIO) s->lead_in_counter++;        // Allow initial estimate to settle
                }
                break;
            case PD_OOK_STATE_PULSE:
                s->pulse_length++;
                // End of pulse detected?
                if (am_n < (ook_threshold - ook_hysteresis)) {    // Gap?
                    // Check for spurious short pulses
                    if (s->pulse_length < PD_MIN_PULSE_SAMPLES) {
                        s->ook_state = PD_OOK_STATE_IDLE;
                    }
                    else {
                        // Continue with OOK decoding
                        pulses->pulse[pulses->num_pulses] = s->pulse_length;    // Store pulse width
                        s->max_pulse = MAX(s->pulse_length, s->max_pulse);    // Find largest pulse
                        s->pulse_length = 0;
                        s->ook_state = PD_OOK_STATE_GAP_START;
                    }
                }
                // Still pulse
                else {
                    // Calculate OOK high level estimate
                    s->ook_high_estimate += am_n / OOK_EST_HIGH_RATIO - s->ook_high_estimate / OOK_EST_HIGH_RATIO;
                    s->ook_high_estimate = MAX(s->ook_high_estimate, pulse_detect->ook_min_high_level);
                    s->ook_high_estimate = MIN(s->ook_high_estimate, OOK_MAX_HIGH_LEVEL);
                    // Estimate pulse carrier frequency
                    pulses->fsk_f1_est += fm_data[s->data_counter] / OOK_EST_HIGH_RATIO - pulses->fsk_f1_est / OOK_EST_HIGH_RATIO;
                }
                // FSK Demodulation
                if (pulses->num_pulses == 0) {    // Only during first pulse
                    if (fpdm == FSK_PULSE_DETECT_OLD)
                        pulse_FSK_detect(fm_data[s->data_counter], fsk_pulses, &s->FSK_state);
                    else
                        pulse_FSK_detect_mm(fm_data[s->data_counter], fsk_pulses, &s->FSK_state);
                }
                break;
            case PD_OOK_STATE_GAP_START:    // Beginning of gap - it might be a spurious gap
                s->pulse_length++;
                // Pulse detected again already? (This is a spurious short gap)
                if (am_n > (ook_threshold + ook_hysteresis)) {    // New pulse?
                    s->pulse_length += pulses->pulse[pulses->num_pulses];    // Restore counter
                    s->ook_state = PD_OOK_STATE_PULSE;
                }
                // Or this gap is for real?
                else if (s->pulse_length >= PD_MIN_PULSE_SAMPLES) {
                    s->ook_state = PD_OOK_STATE_GAP;
                    // Determine if FSK modulation is detected
                    if (fsk_pulses->num_pulses > PD_MIN_PULSES) {
                        // Store last pulse/gap
                        if (fpdm == FSK_PULSE_DETECT_OLD)
                            pulse_FSK_wrap_up(fsk_pulses, &s->FSK_state);
                        // Store estimates
                        fsk_pulses->fsk_f1_est = s->FSK_state.fm_f1_est;
                        fsk_pulses->fsk_f2_est = s->FSK_state.fm_f2_est;
                        fsk_pulses->ook_low_estimate = s->ook_low_estimate;
                        fsk_pulses->ook_high_estimate = s->ook_high_estimate;
                        pulses->end_ago = len - s->data_counter;
                        fsk_pulses->end_ago = len - s->data_counter;
                        s->ook_state = PD_OOK_STATE_IDLE;    // Ensure everything is reset
                        if (pulse_detect->verbosity > 1)
                            print_att_hist("PULSE_DATA_FSK", att_hist);
                        if (pulse_detect->verbosity)
                            fprintf(stderr, "Levels low: -%d dB  high: -%d dB  thres: -%d dB  hyst: (-%d to -%d dB)\n",
                                    mag_to_att(s->ook_low_estimate), mag_to_att(s->ook_high_estimate),
                                    mag_to_att(ook_threshold),
                                    mag_to_att(ook_threshold + ook_hysteresis),
                                    mag_to_att(ook_threshold - ook_hysteresis));
                        return PULSE_DATA_FSK;
                    }
                } // if
                // FSK Demodulation (continue during short gap - we might return...)
                if (pulses->num_pulses == 0) {    // Only during first pulse
                    if (fpdm == FSK_PULSE_DETECT_OLD)
                        pulse_FSK_detect(fm_data[s->data_counter], fsk_pulses, &s->FSK_state);
                    else
                        pulse_FSK_detect_mm(fm_data[s->data_counter], fsk_pulses, &s->FSK_state);
                }
                break;
            case PD_OOK_STATE_GAP:
                s->pulse_length++;
                // New pulse detected?
                if (am_n > (ook_threshold + ook_hysteresis)) {    // New pulse?
                    pulses->gap[pulses->num_pulses] = s->pulse_length;    // Store gap width
                    pulses->num_pulses++;    // Next pulse

                    // EOP if too many pulses
                    if (pulses->num_pulses >= PD_MAX_PULSES) {
                        s->ook_state = PD_OOK_STATE_IDLE;
                        // Store estimates
                        pulses->ook_low_estimate = s->ook_low_estimate;
                        pulses->ook_high_estimate = s->ook_high_estimate;
                        pulses->end_ago = len - s->data_counter;
                        if (pulse_detect->verbosity > 1)
                            print_att_hist("PULSE_DATA_OOK MAX_PULSES", att_hist);
                        return PULSE_DATA_OOK;    // End Of Package!!
                    }

                    s->pulse_length = 0;
                    s->ook_state = PD_OOK_STATE_PULSE;
                }

                // EOP if gap is too long
                if (((s->pulse_length > (PD_MAX_GAP_RATIO * s->max_pulse))    // gap/pulse ratio exceeded
                        && (s->pulse_length > (PD_MIN_GAP_MS * samples_per_ms)))    // Minimum gap exceeded
                        || (s->pulse_length > (PD_MAX_GAP_MS * samples_per_ms))) {    // maximum gap exceeded
                    pulses->gap[pulses->num_pulses] = s->pulse_length;    // Store gap width
                    pulses->num_pulses++;    // Store last pulse
                    s->ook_state = PD_OOK_STATE_IDLE;
                    // Store estimates
                    pulses->ook_low_estimate = s->ook_low_estimate;
                    pulses->ook_high_estimate = s->ook_high_estimate;
                    pulses->end_ago = len - s->data_counter;
                    if (pulse_detect->verbosity > 1)
                        print_att_hist("PULSE_DATA_OOK EOP", att_hist);
                    if (pulse_detect->verbosity)
                        fprintf(stderr, "Levels low: -%d dB  high: -%d dB  thres: -%d dB  hyst: (-%d to -%d dB)\n",
                                mag_to_att(s->ook_low_estimate), mag_to_att(s->ook_high_estimate),
                                mag_to_att(ook_threshold),
                                mag_to_att(ook_threshold + ook_hysteresis),
                                mag_to_att(ook_threshold - ook_hysteresis));
                    return PULSE_DATA_OOK;    // End Of Package!!
                }
                break;
            default:
                fprintf(stderr, "demod_OOK(): Unknown state!!\n");
                s->ook_state = PD_OOK_STATE_IDLE;
        } // switch
        s->data_counter++;
    } // while

    s->data_counter = 0;
    if (pulse_detect->verbosity > 2)
        print_att_hist("Out of data", att_hist);
    return 0;    // Out of data
}
