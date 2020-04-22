/** @file
    Pulse detection functions.

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "pulse_detect.h"
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
    char s[256];
    int i    = 0;
    int size = sizeof(data->pulse) / sizeof(int);

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
    int ook_fixed_high_level; ///< Manual detection level override, 0 = auto.
    int ook_min_high_level;   ///< Minimum estimate of high level (-12 dB: 1000 amp, 4000 mag).
    int ook_high_low_ratio;   ///< Default ratio between high and low (noise) level (9 dB: x8 amp, 11 dB: x3.6 mag).

    enum {
        PD_OOK_STATE_IDLE      = 0,
        PD_OOK_STATE_PULSE     = 1,
        PD_OOK_STATE_GAP_START = 2,
        PD_OOK_STATE_GAP       = 3
    } ook_state;
    int pulse_length; // Counter for internal pulse detection
    int max_pulse;    // Size of biggest pulse detected

    int data_counter;    // Counter for how much of data chunk is processed
    int lead_in_counter; // Counter for allowing initial noise estimate to settle

    int ook_low_estimate;  // Estimate for the OOK low level (base noise level) in the envelope data
    int ook_high_estimate; // Estimate for the OOK high level

    pulse_FSK_state_t FSK_state;
};

pulse_detect_t *pulse_detect_create()
{
    pulse_detect_t *pulse_detect = calloc(1, sizeof(pulse_detect_t));
    if (!pulse_detect) {
        WARN_CALLOC("pulse_detect_create()");
        return NULL;
    }

    pulse_detect_set_levels(pulse_detect, 0, 0.0, -12.1442, 9.0);

    return pulse_detect;
}

void pulse_detect_free(pulse_detect_t *pulse_detect)
{
    free(pulse_detect);
}

void pulse_detect_set_levels(pulse_detect_t *pulse_detect, int use_mag_est, float fixed_high_level, float min_high_level, float high_low_ratio)
{
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

    //fprintf(stderr, "fixed_high_level %.1f (%d), min_high_level %.1f (%d), high_low_ratio %.1f (%d)\n",
    //        fixed_high_level, pulse_detect->ook_fixed_high_level,
    //        min_high_level, pulse_detect->ook_min_high_level,
    //        high_low_ratio, pulse_detect->ook_high_low_ratio);
}

/// Demodulate On/Off Keying (OOK) and Frequency Shift Keying (FSK) from an envelope signal
int pulse_detect_package(pulse_detect_t *pulse_detect, int16_t const *envelope_data, int16_t const *fm_data, int len, uint32_t samp_rate, uint64_t sample_offset, pulse_data_t *pulses, pulse_data_t *fsk_pulses, unsigned fpdm)
{
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
        int16_t ook_threshold = (s->ook_low_estimate + s->ook_high_estimate) / 2;
        if (pulse_detect->ook_fixed_high_level != 0)
            ook_threshold = pulse_detect->ook_fixed_high_level; // Manual override
        int16_t const ook_hysteresis = ook_threshold / 8; // ±12%

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
    return 0;    // Out of data
}


#define MAX_HIST_BINS 16

/// Histogram data for single bin
typedef struct {
    unsigned count;
    int sum;
    int mean;
    int min;
    int max;
} hist_bin_t;

/// Histogram data for all bins
typedef struct {
    unsigned bins_count;
    hist_bin_t bins[MAX_HIST_BINS];
} histogram_t;

/// Generate a histogram (unsorted)
static void histogram_sum(histogram_t *hist, int const *data, unsigned len, float tolerance)
{
    unsigned bin;    // Iterator will be used outside for!

    for (unsigned n = 0; n < len; ++n) {
        // Search for match in existing bins
        for (bin = 0; bin < hist->bins_count; ++bin) {
            int bn = data[n];
            int bm = hist->bins[bin].mean;
            if (abs(bn - bm) < (tolerance * MAX(bn, bm))) {
                hist->bins[bin].count++;
                hist->bins[bin].sum += data[n];
                hist->bins[bin].mean = hist->bins[bin].sum / hist->bins[bin].count;
                hist->bins[bin].min    = MIN(data[n], hist->bins[bin].min);
                hist->bins[bin].max    = MAX(data[n], hist->bins[bin].max);
                break;    // Match found! Data added to existing bin
            }
        }
        // No match found? Add new bin
        if (bin == hist->bins_count && bin < MAX_HIST_BINS) {
            hist->bins[bin].count    = 1;
            hist->bins[bin].sum        = data[n];
            hist->bins[bin].mean    = data[n];
            hist->bins[bin].min        = data[n];
            hist->bins[bin].max        = data[n];
            hist->bins_count++;
        } // for bin
    } // for data
}

/// Delete bin from histogram
static void histogram_delete_bin(histogram_t *hist, unsigned index)
{
    hist_bin_t const zerobin = {0};
    if (hist->bins_count < 1) return;    // Avoid out of bounds
    // Move all bins afterwards one forward
    for (unsigned n = index; n < hist->bins_count-1; ++n) {
        hist->bins[n] = hist->bins[n+1];
    }
    hist->bins_count--;
    hist->bins[hist->bins_count] = zerobin;    // Clear previously last bin
}


/// Swap two bins in histogram
static void histogram_swap_bins(histogram_t *hist, unsigned index1, unsigned index2)
{
    hist_bin_t    tempbin;
    if ((index1 < hist->bins_count) && (index2 < hist->bins_count)) {        // Avoid out of bounds
        tempbin = hist->bins[index1];
        hist->bins[index1] = hist->bins[index2];
        hist->bins[index2] = tempbin;
    }
}


/// Sort histogram with mean value (order lowest to highest)
static void histogram_sort_mean(histogram_t *hist)
{
    if (hist->bins_count < 2) return;        // Avoid underflow
    // Compare all bins (bubble sort)
    for (unsigned n = 0; n < hist->bins_count-1; ++n) {
        for (unsigned m = n+1; m < hist->bins_count; ++m) {
            if (hist->bins[m].mean < hist->bins[n].mean) {
                histogram_swap_bins(hist, m, n);
            }
        }
    }
}


/// Sort histogram with count value (order lowest to highest)
static void histogram_sort_count(histogram_t *hist)
{
    if (hist->bins_count < 2) return;        // Avoid underflow
    // Compare all bins (bubble sort)
    for (unsigned n = 0; n < hist->bins_count-1; ++n) {
        for (unsigned m = n+1; m < hist->bins_count; ++m) {
            if (hist->bins[m].count < hist->bins[n].count) {
                histogram_swap_bins(hist, m, n);
            }
        }
    }
}


/// Fuse histogram bins with means within tolerance
static void histogram_fuse_bins(histogram_t *hist, float tolerance)
{
    if (hist->bins_count < 2) return;        // Avoid underflow
    // Compare all bins
    for (unsigned n = 0; n < hist->bins_count-1; ++n) {
        for (unsigned m = n+1; m < hist->bins_count; ++m) {
            int bn = hist->bins[n].mean;
            int bm = hist->bins[m].mean;
            // if within tolerance
            if (abs(bn - bm) < (tolerance * MAX(bn, bm))) {
                // Fuse data for bin[n] and bin[m]
                hist->bins[n].count += hist->bins[m].count;
                hist->bins[n].sum    += hist->bins[m].sum;
                hist->bins[n].mean    = hist->bins[n].sum / hist->bins[n].count;
                hist->bins[n].min    = MIN(hist->bins[n].min, hist->bins[m].min);
                hist->bins[n].max    = MAX(hist->bins[n].max, hist->bins[m].max);
                // Delete bin[m]
                histogram_delete_bin(hist, m);
                m--;    // Compare new bin in same place!
            }
        }
    }
}

/// Print a histogram
static void histogram_print(histogram_t const *hist, uint32_t samp_rate)
{
    for (unsigned n = 0; n < hist->bins_count; ++n) {
        fprintf(stderr, " [%2u] count: %4u,  width: %4.0f us [%.0f;%.0f]\t(%4i S)\n", n,
                hist->bins[n].count,
                hist->bins[n].mean * 1e6 / samp_rate,
                hist->bins[n].min * 1e6 / samp_rate,
                hist->bins[n].max * 1e6 / samp_rate,
                hist->bins[n].mean);
    }
}

#define TOLERANCE (0.2f) // 20% tolerance should still discern between the pulse widths: 0.33, 0.66, 1.0

/// Analyze the statistics of a pulse data structure and print result
void pulse_analyzer(pulse_data_t *data, int package_type)
{
    double to_ms = 1e3 / data->sample_rate;
    double to_us = 1e6 / data->sample_rate;
    // Generate pulse period data
    int pulse_total_period = 0;
    pulse_data_t pulse_periods = {0};
    pulse_periods.num_pulses = data->num_pulses;
    for (unsigned n = 0; n < pulse_periods.num_pulses; ++n) {
        pulse_periods.pulse[n] = data->pulse[n] + data->gap[n];
        pulse_total_period += data->pulse[n] + data->gap[n];
    }
    pulse_total_period -= data->gap[pulse_periods.num_pulses - 1];

    histogram_t hist_pulses  = {0};
    histogram_t hist_gaps    = {0};
    histogram_t hist_periods = {0};

    // Generate statistics
    histogram_sum(&hist_pulses, data->pulse, data->num_pulses, TOLERANCE);
    histogram_sum(&hist_gaps, data->gap, data->num_pulses - 1, TOLERANCE);                      // Leave out last gap (end)
    histogram_sum(&hist_periods, pulse_periods.pulse, pulse_periods.num_pulses - 1, TOLERANCE); // Leave out last gap (end)

    // Fuse overlapping bins
    histogram_fuse_bins(&hist_pulses, TOLERANCE);
    histogram_fuse_bins(&hist_gaps, TOLERANCE);
    histogram_fuse_bins(&hist_periods, TOLERANCE);

    fprintf(stderr, "Analyzing pulses...\n");
    fprintf(stderr, "Total count: %4u,  width: %4.2f ms\t\t(%5i S)\n",
            data->num_pulses, pulse_total_period * to_ms, pulse_total_period);
    fprintf(stderr, "Pulse width distribution:\n");
    histogram_print(&hist_pulses, data->sample_rate);
    fprintf(stderr, "Gap width distribution:\n");
    histogram_print(&hist_gaps, data->sample_rate);
    fprintf(stderr, "Pulse period distribution:\n");
    histogram_print(&hist_periods, data->sample_rate);
    fprintf(stderr, "Level estimates [high, low]: %6i, %6i\n",
            data->ook_high_estimate, data->ook_low_estimate);
    fprintf(stderr, "RSSI: %.1f dB SNR: %.1f dB Noise: %.1f dB\n",
            data->rssi_db, data->snr_db, data->noise_db);
    fprintf(stderr, "Frequency offsets [F1, F2]:  %6i, %6i\t(%+.1f kHz, %+.1f kHz)\n",
            data->fsk_f1_est, data->fsk_f2_est,
            (float)data->fsk_f1_est / INT16_MAX * data->sample_rate / 2.0 / 1000.0,
            (float)data->fsk_f2_est / INT16_MAX * data->sample_rate / 2.0 / 1000.0);

    fprintf(stderr, "Guessing modulation: ");
    r_device device = {.name = "Analyzer Device", 0};
    histogram_sort_mean(&hist_pulses); // Easier to work with sorted data
    histogram_sort_mean(&hist_gaps);
    if (hist_pulses.bins[0].mean == 0) {
        histogram_delete_bin(&hist_pulses, 0);
    } // Remove FSK initial zero-bin

    // Attempt to find a matching modulation
    if (data->num_pulses == 1) {
        fprintf(stderr, "Single pulse detected. Probably Frequency Shift Keying or just noise...\n");
    }
    else if (hist_pulses.bins_count == 1 && hist_gaps.bins_count == 1) {
        fprintf(stderr, "Un-modulated signal. Maybe a preamble...\n");
    }
    else if (hist_pulses.bins_count == 1 && hist_gaps.bins_count > 1) {
        fprintf(stderr, "Pulse Position Modulation with fixed pulse width\n");
        device.modulation    = OOK_PULSE_PPM;
        device.s_short_width = hist_gaps.bins[0].mean;
        device.s_long_width  = hist_gaps.bins[1].mean;
        device.s_gap_limit   = hist_gaps.bins[1].max + 1;                        // Set limit above next lower gap
        device.s_reset_limit = hist_gaps.bins[hist_gaps.bins_count - 1].max + 1; // Set limit above biggest gap
    }
    else if (hist_pulses.bins_count == 2 && hist_gaps.bins_count == 1) {
        fprintf(stderr, "Pulse Width Modulation with fixed gap\n");
        device.modulation    = OOK_PULSE_PWM;
        device.s_short_width = hist_pulses.bins[0].mean;
        device.s_long_width  = hist_pulses.bins[1].mean;
        device.s_tolerance   = (device.s_long_width - device.s_short_width) * 0.4;
        device.s_reset_limit = hist_gaps.bins[hist_gaps.bins_count - 1].max + 1; // Set limit above biggest gap
    }
    else if (hist_pulses.bins_count == 2 && hist_gaps.bins_count == 2 && hist_periods.bins_count == 1) {
        fprintf(stderr, "Pulse Width Modulation with fixed period\n");
        device.modulation    = OOK_PULSE_PWM;
        device.s_short_width = hist_pulses.bins[0].mean;
        device.s_long_width  = hist_pulses.bins[1].mean;
        device.s_tolerance   = (device.s_long_width - device.s_short_width) * 0.4;
        device.s_reset_limit = hist_gaps.bins[hist_gaps.bins_count - 1].max + 1; // Set limit above biggest gap
    }
    else if (hist_pulses.bins_count == 2 && hist_gaps.bins_count == 2 && hist_periods.bins_count == 3) {
        fprintf(stderr, "Manchester coding\n");
        device.modulation    = OOK_PULSE_MANCHESTER_ZEROBIT;
        device.s_short_width = MIN(hist_pulses.bins[0].mean, hist_pulses.bins[1].mean); // Assume shortest pulse is half period
        device.s_long_width  = 0;                                                       // Not used
        device.s_reset_limit = hist_gaps.bins[hist_gaps.bins_count - 1].max + 1;        // Set limit above biggest gap
    }
    else if (hist_pulses.bins_count == 2 && hist_gaps.bins_count >= 3) {
        fprintf(stderr, "Pulse Width Modulation with multiple packets\n");
        device.modulation    = (package_type == PULSE_DATA_FSK) ? FSK_PULSE_PWM : OOK_PULSE_PWM;
        device.s_short_width = hist_pulses.bins[0].mean;
        device.s_long_width  = hist_pulses.bins[1].mean;
        device.s_gap_limit   = hist_gaps.bins[1].max + 1; // Set limit above second gap
        device.s_tolerance   = (device.s_long_width - device.s_short_width) * 0.4;
        device.s_reset_limit = hist_gaps.bins[hist_gaps.bins_count - 1].max + 1; // Set limit above biggest gap
    }
    else if ((hist_pulses.bins_count >= 3 && hist_gaps.bins_count >= 3)
            && (abs(hist_pulses.bins[1].mean - 2*hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean/8)    // Pulses are multiples of shortest pulse
            && (abs(hist_pulses.bins[2].mean - 3*hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean/8)
            && (abs(hist_gaps.bins[0].mean   -   hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean/8)    // Gaps are multiples of shortest pulse
            && (abs(hist_gaps.bins[1].mean   - 2*hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean/8)
            && (abs(hist_gaps.bins[2].mean   - 3*hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean/8)) {
        fprintf(stderr, "Pulse Code Modulation (Not Return to Zero)\n");
        device.modulation    = FSK_PULSE_PCM;
        device.s_short_width = hist_pulses.bins[0].mean;        // Shortest pulse is bit width
        device.s_long_width  = hist_pulses.bins[0].mean;        // Bit period equal to pulse length (NRZ)
        device.s_reset_limit = hist_pulses.bins[0].mean * 1024; // No limit to run of zeros...
    }
    else if (hist_pulses.bins_count == 3) {
        fprintf(stderr, "Pulse Width Modulation with sync/delimiter\n");
        // Re-sort to find lowest pulse count index (is probably delimiter)
        histogram_sort_count(&hist_pulses);
        int p1 = hist_pulses.bins[1].mean;
        int p2 = hist_pulses.bins[2].mean;
        device.modulation    = OOK_PULSE_PWM;
        device.s_short_width = p1 < p2 ? p1 : p2;                                // Set to shorter pulse width
        device.s_long_width  = p1 < p2 ? p2 : p1;                                // Set to longer pulse width
        device.s_sync_width  = hist_pulses.bins[0].mean;                         // Set to lowest count pulse width
        device.s_reset_limit = hist_gaps.bins[hist_gaps.bins_count - 1].max + 1; // Set limit above biggest gap
    }
    else {
        fprintf(stderr, "No clue...\n");
    }

    // Demodulate (if detected)
    if (device.modulation) {
        fprintf(stderr, "Attempting demodulation... short_width: %.0f, long_width: %.0f, reset_limit: %.0f, sync_width: %.0f\n",
                device.s_short_width * to_us, device.s_long_width * to_us,
                device.s_reset_limit * to_us, device.s_sync_width * to_us);
        switch (device.modulation) {
        case FSK_PULSE_PCM:
            fprintf(stderr, "Use a flex decoder with -X 'n=name,m=FSK_PCM,s=%.0f,l=%.0f,r=%.0f'\n",
                    device.s_short_width * to_us, device.s_long_width * to_us, device.s_reset_limit * to_us);
            pulse_demod_pcm(data, &device);
            break;
        case OOK_PULSE_PPM:
            fprintf(stderr, "Use a flex decoder with -X 'n=name,m=OOK_PPM,s=%.0f,l=%.0f,g=%.0f,r=%.0f'\n",
                    device.s_short_width * to_us, device.s_long_width * to_us,
                    device.s_gap_limit * to_us, device.s_reset_limit * to_us);
            data->gap[data->num_pulses - 1] = device.s_reset_limit + 1; // Be sure to terminate package
            pulse_demod_ppm(data, &device);
            break;
        case OOK_PULSE_PWM:
            fprintf(stderr, "Use a flex decoder with -X 'n=name,m=OOK_PWM,s=%.0f,l=%.0f,r=%.0f,g=%.0f,t=%.0f,y=%.0f'\n",
                    device.s_short_width * to_us, device.s_long_width * to_us, device.s_reset_limit * to_us,
                    device.s_gap_limit * to_us, device.s_tolerance * to_us, device.s_sync_width * to_us);
            data->gap[data->num_pulses - 1] = device.s_reset_limit + 1; // Be sure to terminate package
            pulse_demod_pwm(data, &device);
            break;
        case FSK_PULSE_PWM:
            fprintf(stderr, "Use a flex decoder with -X 'n=name,m=FSK_PWM,s=%.0f,l=%.0f,r=%.0f,g=%.0f,t=%.0f,y=%.0f'\n",
                    device.s_short_width * to_us, device.s_long_width * to_us, device.s_reset_limit * to_us,
                    device.s_gap_limit * to_us, device.s_tolerance * to_us, device.s_sync_width * to_us);
            data->gap[data->num_pulses - 1] = device.s_reset_limit + 1; // Be sure to terminate package
            pulse_demod_pwm(data, &device);
            break;
        case OOK_PULSE_MANCHESTER_ZEROBIT:
            fprintf(stderr, "Use a flex decoder with -X 'n=name,m=OOK_MC_ZEROBIT,s=%.0f,l=%.0f,r=%.0f'\n",
                    device.s_short_width * to_us, device.s_long_width * to_us, device.s_reset_limit * to_us);
            data->gap[data->num_pulses - 1] = device.s_reset_limit + 1; // Be sure to terminate package
            pulse_demod_manchester_zerobit(data, &device);
            break;
        default:
            fprintf(stderr, "Unsupported\n");
        }
    }

    fprintf(stderr, "\n");
}
