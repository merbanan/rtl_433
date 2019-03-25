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
#include "util.h"
#include "decoder.h"
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
        fprintf(stderr, "[%3u] Pulse: %4u, Gap: %4u, Period: %4u\n", n, data->pulse[n], data->gap[n], data->pulse[n] + data->gap[n]);
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

void pulse_data_print_vcd_header(FILE *file, uint32_t sample_rate)
{
    char time_str[LOCAL_TIME_BUFLEN];
    char *timescale;
    if (sample_rate <= 500000)
        timescale = "1 us";
    else
        timescale = "100 ns";
    fprintf(file, "$date %s $end\n", local_time_str(0, time_str));
    fprintf(file, "$version rtl_433 0.1.0 $end\n");
    fprintf(file, "$comment Acquisition at %s Hz $end\n", nice_freq(sample_rate));
    fprintf(file, "$timescale %s $end\n", timescale);
    fprintf(file, "$scope module rtl_433 $end\n");
    fprintf(file, "$var wire 1 / FRAME $end\n");
    fprintf(file, "$var wire 1 ' AM $end\n");
    fprintf(file, "$var wire 1 \" FM $end\n");
    fprintf(file, "$upscope $end\n");
    fprintf(file, "$enddefinitions $end\n");
    fprintf(file, "#0 0/ 0' 0\"\n");
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
            fprintf(file, "#%.f 1/ 1%c\n", pos * scale, ch_id);
        else
            fprintf(file, "#%.f 1%c\n", pos * scale, ch_id);
        pos += data->pulse[n];
        fprintf(file, "#%.f 0%c\n", pos * scale, ch_id);
        pos += data->gap[n];
    }
    if (data->num_pulses > 0)
        fprintf(file, "#%.f 0/\n", pos * scale);
}

void pulse_data_load(FILE *file, pulse_data_t *data)
{
    char s[256];
    int i    = 0;
    int size = sizeof(data->pulse) / sizeof(int);

    pulse_data_clear(data);
    data->sample_rate = 1000000; // assumes 1us timescale
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
        data->pulse[i] = (int)mark;
        data->gap[i++] = (int)space;
    }
    //fprintf(stderr, "read %d pulses\n", i);
    data->num_pulses = i;
}

void pulse_data_print_pulse_header(FILE *file)
{
    char time_str[LOCAL_TIME_BUFLEN];

    fprintf(file, ";pulse data\n");
    fprintf(file, ";version 1\n");
    fprintf(file, ";timescale 1us\n");
    //fprintf(file, ";samplerate %u\n", data->sample_rate);
    fprintf(file, ";created %s\n", local_time_str(0, time_str));
}

void pulse_data_dump(FILE *file, pulse_data_t *data)
{
    if (data->fsk_f2_est) {
        fprintf(file, ";fsk %d pulses\n", data->num_pulses);
        fprintf(file, ";freq1 %.0f\n", data->freq1_hz);
        fprintf(file, ";freq2 %.0f\n", data->freq2_hz);
    }
    else {
        fprintf(file, ";ook %d pulses\n", data->num_pulses);
        fprintf(file, ";freq1 %.0f\n", data->freq1_hz);
    }
    double to_us = 1e6 / data->sample_rate;
    for (unsigned i = 0; i < data->num_pulses; ++i) {
        fprintf(file, "%.0f %.0f\n", data->pulse[i] * to_us, data->gap[i] * to_us);
    }
    fprintf(file, ";end\n");
}

// OOK adaptive level estimator constants
#define OOK_HIGH_LOW_RATIO  8           // Default ratio between high and low (noise) level
#define OOK_MIN_HIGH_LEVEL  1000        // Minimum estimate of high level
#define OOK_MAX_HIGH_LEVEL  (128*128)   // Maximum estimate for high level (A unit phasor is 128, anything above is overdrive)
#define OOK_MAX_LOW_LEVEL   (OOK_MAX_HIGH_LEVEL/2)    // Maximum estimate for low level
#define OOK_EST_HIGH_RATIO  64          // Constant for slowness of OOK high level estimator
#define OOK_EST_LOW_RATIO   1024        // Constant for slowness of OOK low level (noise) estimator (very slow)

// FSK adaptive frequency estimator constants
#define FSK_DEFAULT_FM_DELTA 6000       // Default estimate for frequency delta
#define FSK_EST_SLOW        64         // Constant for slowness of FSK estimators
#define FSK_EST_FAST        16          // Constant for slowness of FSK estimators

/// Internal state data for pulse_FSK_detect()
typedef struct {
    unsigned int fsk_pulse_length; ///< Counter for internal FSK pulse detection
    enum {
        PD_FSK_STATE_INIT  = 0, ///< Initial frequency estimation
        PD_FSK_STATE_F1    = 1, ///< High frequency (pulse)
        PD_FSK_STATE_F2    = 2, ///< Low frequency (gap)
        PD_FSK_STATE_ERROR = 3  ///< Error - stay here until cleared
    } fsk_state;

    int fm_f1_est; ///< Estimate for the F1 frequency for FSK
    int fm_f2_est; ///< Estimate for the F2 frequency for FSK

} pulse_FSK_state_t;

/// Demodulate Frequency Shift Keying (FSK) sample by sample
///
/// Function is stateful between calls
/// Builds estimate for initial frequency. When frequency deviates more than a
/// threshold value it will determine whether the deviation is positive or negative
/// to classify it as a pulse or gap. It will then transition to other state (F1 or F2)
/// and build an estimate of the other frequency. It will then transition back and forth when current
/// frequency is closer to other frequency estimate.
/// Includes spurious suppression by coalescing pulses when pulse/gap widths are too short.
/// Pulses equal higher frequency (F1) and Gaps equal lower frequency (F2)
/// @param fm_n: One single sample of FM data
/// @param *fsk_pulses: Will return a pulse_data_t structure for FSK demodulated data
/// @param *s: Internal state
void pulse_FSK_detect(int16_t fm_n, pulse_data_t *fsk_pulses, pulse_FSK_state_t *s)
{
    int const fm_f1_delta = abs(fm_n - s->fm_f1_est); // Get delta from F1 frequency estimate
    int const fm_f2_delta = abs(fm_n - s->fm_f2_est); // Get delta from F2 frequency estimate
    s->fsk_pulse_length++;

    switch(s->fsk_state) {
        case PD_FSK_STATE_INIT:        // Initial frequency - High or low?
            // Initial samples?
            if (s->fsk_pulse_length < PD_MIN_PULSE_SAMPLES) {
                s->fm_f1_est = s->fm_f1_est/2 + fm_n/2;        // Quick initial estimator
            }
            // Above default frequency delta?
            else if (fm_f1_delta > (FSK_DEFAULT_FM_DELTA/2)) {
                // Positive frequency delta - Initial frequency was low (gap)
                if (fm_n > s->fm_f1_est) {
                    s->fsk_state = PD_FSK_STATE_F1;
                    s->fm_f2_est = s->fm_f1_est;    // Switch estimates
                    s->fm_f1_est = fm_n;            // Prime F1 estimate
                    fsk_pulses->pulse[0] = 0;        // Initial frequency was a gap...
                    fsk_pulses->gap[0] = s->fsk_pulse_length;        // Store gap width
                    fsk_pulses->num_pulses++;
                    s->fsk_pulse_length = 0;
                }
                // Negative Frequency delta - Initial frequency was high (pulse)
                else {
                    s->fsk_state = PD_FSK_STATE_F2;
                    s->fm_f2_est = fm_n;    // Prime F2 estimate
                    fsk_pulses->pulse[0] = s->fsk_pulse_length;    // Store pulse width
                    s->fsk_pulse_length = 0;
                }
            }
            // Still below threshold
            else {
                s->fm_f1_est += fm_n/FSK_EST_FAST - s->fm_f1_est/FSK_EST_FAST;    // Fast estimator
            }
            break;
        case PD_FSK_STATE_F1:        // Pulse high at F1 frequency
            // Closer to F2 than F1?
            if (fm_f1_delta > fm_f2_delta) {
                s->fsk_state = PD_FSK_STATE_F2;
                // Store if pulse is not too short (suppress spurious)
                if (s->fsk_pulse_length >= PD_MIN_PULSE_SAMPLES) {
                    fsk_pulses->pulse[fsk_pulses->num_pulses] = s->fsk_pulse_length;    // Store pulse width
                    s->fsk_pulse_length = 0;
                }
                // Else rewind to last gap
                else {
                    s->fsk_pulse_length += fsk_pulses->gap[fsk_pulses->num_pulses-1];    // Restore counter
                    fsk_pulses->num_pulses--;        // Rewind one pulse
                    // Are we back to initial frequency? (Was initial frequency a gap?)
                    if ((fsk_pulses->num_pulses == 0) && (fsk_pulses->pulse[0] == 0)) {
                        s->fm_f1_est = s->fm_f2_est;    // Switch back estimates
                        s->fsk_state = PD_FSK_STATE_INIT;
                    }
                }
            }
            // Still below threshold
            else {
                if (fm_n > s->fm_f1_est)
                    s->fm_f1_est += fm_n/FSK_EST_FAST - s->fm_f1_est/FSK_EST_FAST;    // Fast estimator
                else
                    s->fm_f1_est += fm_n/FSK_EST_SLOW - s->fm_f1_est/FSK_EST_SLOW;    // Slow estimator
            }
            break;
        case PD_FSK_STATE_F2:        // Pulse gap at F2 frequency
            // Freq closer to F1 than F2 ?
            if (fm_f2_delta > fm_f1_delta) {
                s->fsk_state = PD_FSK_STATE_F1;
                // Store if pulse is not too short (suppress spurious)
                if (s->fsk_pulse_length >= PD_MIN_PULSE_SAMPLES) {
                    fsk_pulses->gap[fsk_pulses->num_pulses] = s->fsk_pulse_length;    // Store gap width
                    fsk_pulses->num_pulses++;    // Go to next pulse
                    s->fsk_pulse_length = 0;
                    // When pulse buffer is full go to error state
                    if (fsk_pulses->num_pulses >= PD_MAX_PULSES) {
                        fprintf(stderr, "pulse_FSK_detect(): Maximum number of pulses reached!\n");
                        s->fsk_state = PD_FSK_STATE_ERROR;
                    }
                }
                // Else rewind to last pulse
                else {
                    s->fsk_pulse_length += fsk_pulses->pulse[fsk_pulses->num_pulses];    // Restore counter
                    // Are we back to initial frequency?
                    if (fsk_pulses->num_pulses == 0) {
                        s->fsk_state = PD_FSK_STATE_INIT;
                    }
                }
            }
            // Still below threshold
            else {
                if (fm_n < s->fm_f2_est)
                    s->fm_f2_est += fm_n/FSK_EST_FAST - s->fm_f2_est/FSK_EST_FAST;    // Fast estimator
                else
                    s->fm_f2_est += fm_n/FSK_EST_SLOW - s->fm_f2_est/FSK_EST_SLOW;    // Slow estimator
            }
            break;
        case PD_FSK_STATE_ERROR:        // Stay here until cleared
            break;
        default:
            fprintf(stderr, "pulse_FSK_detect(): Unknown FSK state!!\n");
            s->fsk_state = PD_FSK_STATE_ERROR;
    } // switch(s->fsk_state)
}

/// Wrap up FSK modulation and store last data at End Of Package
///
/// @param fm_n: One single sample of FM data
/// @param *fsk_pulses: Pulse_data_t structure for FSK demodulated data
/// @param *s: Internal state
void pulse_FSK_wrap_up(pulse_data_t *fsk_pulses, pulse_FSK_state_t *s)
{
    if (fsk_pulses->num_pulses < PD_MAX_PULSES) { // Avoid overflow
        s->fsk_pulse_length++;
        if (s->fsk_state == PD_FSK_STATE_F1) {
            fsk_pulses->pulse[fsk_pulses->num_pulses] = s->fsk_pulse_length; // Store last pulse
            fsk_pulses->gap[fsk_pulses->num_pulses]   = 0;                   // Zero gap at end
        }
        else {
            fsk_pulses->gap[fsk_pulses->num_pulses] = s->fsk_pulse_length; // Store last gap
        }
        fsk_pulses->num_pulses++;
    }
}

/// Internal state data for pulse_pulse_package()
struct pulse_detect {
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
    return calloc(1, sizeof(pulse_detect_t));
}

void pulse_detect_free(pulse_detect_t *pulse_detect)
{
    free(pulse_detect);
}

/// Demodulate On/Off Keying (OOK) and Frequency Shift Keying (FSK) from an envelope signal
int pulse_detect_package(pulse_detect_t *pulse_detect, int16_t const *envelope_data, int16_t const *fm_data, int len, int16_t level_limit, uint32_t samp_rate, uint64_t sample_offset, pulse_data_t *pulses, pulse_data_t *fsk_pulses)
{
    int const samples_per_ms = samp_rate / 1000;
    pulse_detect_t *s = pulse_detect;
    s->ook_high_estimate = MAX(s->ook_high_estimate, OOK_MIN_HIGH_LEVEL);    // Be sure to set initial minimum level

    if (s->data_counter == 0) {
        // age the pulse_data if this is a fresh buffer
        pulses->start_ago += len;
        fsk_pulses->start_ago += len;
    }

    // Process all new samples
    while (s->data_counter < len) {
        // Calculate OOK detection threshold and hysteresis
        int16_t const am_n    = envelope_data[s->data_counter];
        int16_t ook_threshold = s->ook_low_estimate + (s->ook_high_estimate - s->ook_low_estimate) / 2;
        if (level_limit != 0)
            ook_threshold = level_limit;                  // Manual override
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
                    s->ook_state = PD_OOK_STATE_PULSE;
                }
                else {    // We are still idle..
                    // Estimate low (noise) level
                    int const ook_low_delta = am_n - s->ook_low_estimate;
                    s->ook_low_estimate += ook_low_delta / OOK_EST_LOW_RATIO;
                    s->ook_low_estimate += ((ook_low_delta > 0) ? 1 : -1);    // Hack to compensate for lack of fixed-point scaling
                    // Calculate default OOK high level estimate
                    s->ook_high_estimate = OOK_HIGH_LOW_RATIO * s->ook_low_estimate;    // Default is a ratio of low level
                    s->ook_high_estimate = MAX(s->ook_high_estimate, OOK_MIN_HIGH_LEVEL);
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
                    s->ook_high_estimate = MAX(s->ook_high_estimate, OOK_MIN_HIGH_LEVEL);
                    s->ook_high_estimate = MIN(s->ook_high_estimate, OOK_MAX_HIGH_LEVEL);
                    // Estimate pulse carrier frequency
                    pulses->fsk_f1_est += fm_data[s->data_counter] / OOK_EST_HIGH_RATIO - pulses->fsk_f1_est / OOK_EST_HIGH_RATIO;
                }
                // FSK Demodulation
                if (pulses->num_pulses == 0) {    // Only during first pulse
                    pulse_FSK_detect(fm_data[s->data_counter], fsk_pulses, &s->FSK_state);
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
                        pulse_FSK_wrap_up(fsk_pulses, &s->FSK_state);
                        // Store estimates
                        fsk_pulses->fsk_f1_est = s->FSK_state.fm_f1_est;
                        fsk_pulses->fsk_f2_est = s->FSK_state.fm_f2_est;
                        fsk_pulses->ook_low_estimate = s->ook_low_estimate;
                        fsk_pulses->ook_high_estimate = s->ook_high_estimate;
                        pulses->end_ago = len - s->data_counter;
                        fsk_pulses->end_ago = len - s->data_counter;
                        s->ook_state = PD_OOK_STATE_IDLE;    // Ensure everything is reset
                        return 2;    // FSK package detected!!!
                    }
                } // if
                // FSK Demodulation (continue during short gap - we might return...)
                if (pulses->num_pulses == 0) {    // Only during first pulse
                    pulse_FSK_detect(fm_data[s->data_counter], fsk_pulses, &s->FSK_state);
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
                        return 1;    // End Of Package!!
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
                    return 1;    // End Of Package!!
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
void histogram_sum(histogram_t *hist, int const *data, unsigned len, float tolerance)
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
void histogram_delete_bin(histogram_t *hist, unsigned index)
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
void histogram_swap_bins(histogram_t *hist, unsigned index1, unsigned index2)
{
    hist_bin_t    tempbin;
    if ((index1 < hist->bins_count) && (index2 < hist->bins_count)) {        // Avoid out of bounds
        tempbin = hist->bins[index1];
        hist->bins[index1] = hist->bins[index2];
        hist->bins[index2] = tempbin;
    }
}


/// Sort histogram with mean value (order lowest to highest)
void histogram_sort_mean(histogram_t *hist)
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
void histogram_sort_count(histogram_t *hist)
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
void histogram_fuse_bins(histogram_t *hist, float tolerance)
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
void histogram_print(histogram_t const *hist, uint32_t samp_rate)
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
void pulse_analyzer(pulse_data_t *data)
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
        device.modulation    = OOK_PULSE_PWM;
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
