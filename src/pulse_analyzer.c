/** @file
    Pulse analyzer functions.

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "pulse_analyzer.h"
#include "pulse_slicer.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

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

/// Find bin index
static int histogram_find_bin_index(histogram_t const *hist, int width)
{
    for (unsigned n = 0; n < hist->bins_count; ++n) {
        if (hist->bins[n].min <= width && width <= hist->bins[n].max) {
            return n;
        }
    }
    return -1;
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

#define HEXSTR_BUILDER_SIZE 1024
#define HEXSTR_MAX_COUNT 32

/// Hex string builder
typedef struct hexstr {
    uint8_t p[HEXSTR_BUILDER_SIZE];
    unsigned idx;
} hexstr_t;

static void hexstr_push_byte(hexstr_t *h, uint8_t v)
{
    if (h->idx < HEXSTR_BUILDER_SIZE)
        h->p[h->idx++] = v;
}

static void hexstr_push_word(hexstr_t *h, uint16_t v)
{
    if (h->idx + 1 < HEXSTR_BUILDER_SIZE) {
        h->p[h->idx++] = v >> 8;
        h->p[h->idx++] = v & 0xff;
    }
}

static void hexstr_print(hexstr_t *h, FILE *out)
{
    for (unsigned i = 0; i < h->idx; ++i)
        fprintf(out, "%02X", h->p[i]);
}

#define TOLERANCE (0.2f) // 20% tolerance should still discern between the pulse widths: 0.33, 0.66, 1.0

/// Analyze the statistics of a pulse data structure and print result
void pulse_analyzer(pulse_data_t *data, int package_type)
{
    if (data->num_pulses == 0) {
        fprintf(stderr, "No pulses detected.\n");
        return;
    }

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
    histogram_t hist_timings = {0};

    // Generate statistics
    histogram_sum(&hist_pulses, data->pulse, data->num_pulses, TOLERANCE);
    histogram_sum(&hist_gaps, data->gap, data->num_pulses - 1, TOLERANCE);                      // Leave out last gap (end)
    histogram_sum(&hist_periods, pulse_periods.pulse, pulse_periods.num_pulses - 1, TOLERANCE); // Leave out last gap (end)
    histogram_sum(&hist_timings, data->pulse, data->num_pulses, TOLERANCE);
    histogram_sum(&hist_timings, data->gap, data->num_pulses, TOLERANCE);

    // Fuse overlapping bins
    histogram_fuse_bins(&hist_pulses, TOLERANCE);
    histogram_fuse_bins(&hist_gaps, TOLERANCE);
    histogram_fuse_bins(&hist_periods, TOLERANCE);
    histogram_fuse_bins(&hist_timings, TOLERANCE);

    fprintf(stderr, "Analyzing pulses...\n");
    fprintf(stderr, "Total count: %4u,  width: %4.2f ms\t\t(%5i S)\n",
            data->num_pulses, pulse_total_period * to_ms, pulse_total_period);
    fprintf(stderr, "Pulse width distribution:\n");
    histogram_print(&hist_pulses, data->sample_rate);
    fprintf(stderr, "Gap width distribution:\n");
    histogram_print(&hist_gaps, data->sample_rate);
    fprintf(stderr, "Pulse period distribution:\n");
    histogram_print(&hist_periods, data->sample_rate);
    fprintf(stderr, "Pulse timing distribution:\n");
    histogram_print(&hist_timings, data->sample_rate);
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
        device.modulation  = OOK_PULSE_PPM; // TODO: there is not FSK_PULSE_PPM
        device.short_width = to_us * hist_gaps.bins[0].mean;
        device.long_width  = to_us * hist_gaps.bins[1].mean;
        device.gap_limit   = to_us * (hist_gaps.bins[1].max + 1);                        // Set limit above next lower gap
        device.reset_limit = to_us * (hist_gaps.bins[hist_gaps.bins_count - 1].max + 1); // Set limit above biggest gap
    }
    else if (hist_pulses.bins_count == 2 && hist_gaps.bins_count == 1) {
        fprintf(stderr, "Pulse Width Modulation with fixed gap\n");
        device.modulation  = (package_type == PULSE_DATA_FSK) ? FSK_PULSE_PWM : OOK_PULSE_PWM;
        device.short_width = to_us * hist_pulses.bins[0].mean;
        device.long_width  = to_us * hist_pulses.bins[1].mean;
        device.tolerance   = (device.long_width - device.short_width) * 0.4;
        device.reset_limit = to_us * (hist_gaps.bins[hist_gaps.bins_count - 1].max + 1); // Set limit above biggest gap
    }
    else if (hist_pulses.bins_count == 2 && hist_gaps.bins_count == 2 && hist_periods.bins_count == 1) {
        fprintf(stderr, "Pulse Width Modulation with fixed period\n");
        device.modulation  = (package_type == PULSE_DATA_FSK) ? FSK_PULSE_PWM : OOK_PULSE_PWM;
        device.short_width = to_us * hist_pulses.bins[0].mean;
        device.long_width  = to_us * hist_pulses.bins[1].mean;
        device.tolerance   = (device.long_width - device.short_width) * 0.4;
        device.reset_limit = to_us * (hist_gaps.bins[hist_gaps.bins_count - 1].max + 1); // Set limit above biggest gap
    }
    else if (hist_pulses.bins_count == 2 && hist_gaps.bins_count == 2 && hist_periods.bins_count == 3) {
        fprintf(stderr, "Manchester coding\n");
        device.modulation  = (package_type == PULSE_DATA_FSK) ? FSK_PULSE_MANCHESTER_ZEROBIT : OOK_PULSE_MANCHESTER_ZEROBIT;
        device.short_width = to_us * MIN(hist_pulses.bins[0].mean, hist_pulses.bins[1].mean); // Assume shortest pulse is half period
        device.long_width  = 0;                                                               // Not used
        device.reset_limit = to_us * (hist_gaps.bins[hist_gaps.bins_count - 1].max + 1);      // Set limit above biggest gap
    }
    else if (hist_pulses.bins_count == 2 && hist_gaps.bins_count >= 3) {
        fprintf(stderr, "Pulse Width Modulation with multiple packets\n");
        device.modulation  = (package_type == PULSE_DATA_FSK) ? FSK_PULSE_PWM : OOK_PULSE_PWM;
        device.short_width = to_us * hist_pulses.bins[0].mean;
        device.long_width  = to_us * hist_pulses.bins[1].mean;
        device.gap_limit   = to_us * (hist_gaps.bins[1].max + 1); // Set limit above second gap
        device.tolerance   = (device.long_width - device.short_width) * 0.4;
        device.reset_limit = to_us * (hist_gaps.bins[hist_gaps.bins_count - 1].max + 1); // Set limit above biggest gap
    }
    else if ((hist_pulses.bins_count >= 3 && hist_gaps.bins_count >= 3)
            && (abs(hist_pulses.bins[1].mean - 2*hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean/8)    // Pulses are multiples of shortest pulse
            && (abs(hist_pulses.bins[2].mean - 3*hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean/8)
            && (abs(hist_gaps.bins[0].mean   -   hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean/8)    // Gaps are multiples of shortest pulse
            && (abs(hist_gaps.bins[1].mean   - 2*hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean/8)
            && (abs(hist_gaps.bins[2].mean   - 3*hist_pulses.bins[0].mean) <= hist_pulses.bins[0].mean/8)) {
        fprintf(stderr, "Non Return to Zero coding (Pulse Code)\n");
        device.modulation  = (package_type == PULSE_DATA_FSK) ? FSK_PULSE_PCM : OOK_PULSE_PCM;
        device.short_width = to_us * hist_pulses.bins[0].mean;        // Shortest pulse is bit width
        device.long_width  = to_us * hist_pulses.bins[0].mean;        // Bit period equal to pulse length (NRZ)
        device.reset_limit = to_us * hist_pulses.bins[0].mean * 1024; // No limit to run of zeros...
    }
    else if (hist_pulses.bins_count == 3) {
        fprintf(stderr, "Pulse Width Modulation with sync/delimiter\n");
        // Re-sort to find lowest pulse count index (is probably delimiter)
        histogram_sort_count(&hist_pulses);
        int p1 = hist_pulses.bins[1].mean;
        int p2 = hist_pulses.bins[2].mean;
        device.modulation  = (package_type == PULSE_DATA_FSK) ? FSK_PULSE_PWM : OOK_PULSE_PWM;
        device.short_width = to_us * (p1 < p2 ? p1 : p2);                                // Set to shorter pulse width
        device.long_width  = to_us * (p1 < p2 ? p2 : p1);                                // Set to longer pulse width
        device.sync_width  = to_us * hist_pulses.bins[0].mean;                           // Set to lowest count pulse width
        device.reset_limit = to_us * (hist_gaps.bins[hist_gaps.bins_count - 1].max + 1); // Set limit above biggest gap
    }
    else {
        fprintf(stderr, "No clue...\n");
    }

    // Output RfRaw line (if possible)
    if (hist_timings.bins_count <= 8) {
        // if there is no 3rd gap length output one long B1 code
        if (hist_gaps.bins_count <= 2) {
            hexstr_t hexstr = {.p = {0}};
            hexstr_push_byte(&hexstr, 0xaa);
            hexstr_push_byte(&hexstr, 0xb1);
            hexstr_push_byte(&hexstr, hist_timings.bins_count);
            for (unsigned b = 0; b < hist_timings.bins_count; ++b) {
                double w = hist_timings.bins[b].mean * to_us;
                hexstr_push_word(&hexstr, w < USHRT_MAX ? w : USHRT_MAX);
            }
            for (unsigned i = 0; i < data->num_pulses; ++i) {
                int p = histogram_find_bin_index(&hist_timings, data->pulse[i]);
                int g = histogram_find_bin_index(&hist_timings, data->gap[i]);
                if (p < 0 || g < 0) {
                    fprintf(stderr, "%s: this can't happen\n", __func__);
                    exit(1);
                }
                hexstr_push_byte(&hexstr, 0x80 | (p << 4) | g);
            }
            hexstr_push_byte(&hexstr, 0x55);
            fprintf(stderr, "view at https://triq.org/pdv/#");
            hexstr_print(&hexstr, stderr);
            fprintf(stderr, "\n");
        }
        // otherwise try to group as B0 codes
        else {
            // pick last gap length but a most the 4th
            int limit_bin = MIN(3, hist_gaps.bins_count - 1);
            int limit = hist_gaps.bins[limit_bin].min;
            hexstr_t hexstrs[HEXSTR_MAX_COUNT] = {{.p = {0}}};
            unsigned hexstr_cnt = 0;
            unsigned i = 0;
            while (i < data->num_pulses && hexstr_cnt < HEXSTR_MAX_COUNT) {
                hexstr_t *hexstr = &hexstrs[hexstr_cnt];
                hexstr_push_byte(hexstr, 0xaa);
                hexstr_push_byte(hexstr, 0xb0);
                hexstr_push_byte(hexstr, 0); // len
                hexstr_push_byte(hexstr, hist_timings.bins_count);
                hexstr_push_byte(hexstr, 1); // repeats
                for (unsigned b = 0; b < hist_timings.bins_count; ++b) {
                    double w =hist_timings.bins[b].mean * to_us;
                    hexstr_push_word(hexstr, w < USHRT_MAX ? w : USHRT_MAX);
                }
                for (; i < data->num_pulses; ++i) {
                    int p = histogram_find_bin_index(&hist_timings, data->pulse[i]);
                    int g = histogram_find_bin_index(&hist_timings, data->gap[i]);
                    if (p < 0 || g < 0) {
                        fprintf(stderr, "%s: this can't happen\n", __func__);
                        exit(1);
                    }
                    hexstr_push_byte(hexstr, 0x80 | (p << 4) | g);
                    if (data->gap[i] >= limit) {
                        ++i;
                        break;
                    }
                }
                hexstr_push_byte(hexstr, 0x55);
                hexstr->p[2] = hexstr->idx - 4 <= 255 ? hexstr->idx - 4 : 0; // len
                if (hexstr_cnt > 0 && hexstrs[hexstr_cnt - 1].idx == hexstr->idx
                        && !memcmp(&hexstrs[hexstr_cnt - 1].p[5], &hexstr->p[5], hexstr->idx - 5)) {
                    hexstr->idx = 0; // clear
                    hexstrs[hexstr_cnt - 1].p[4] += 1; // repeats
                } else {
                    hexstr_cnt++;
                }
            }

            fprintf(stderr, "view at https://triq.org/pdv/#");
            for (unsigned j = 0; j < hexstr_cnt; ++j) {
                if (j > 0)
                    fprintf(stderr, "+");
                hexstr_print(&hexstrs[j], stderr);
            }
            fprintf(stderr, "\n");
            if (hexstr_cnt >= HEXSTR_MAX_COUNT) {
                fprintf(stderr, "Too many pulse groups (%u pulses missed in rfraw)\n", data->num_pulses - i);
            }
        }
    }

    // Demodulate (if detected)
    if (device.modulation) {
        fprintf(stderr, "Attempting demodulation... short_width: %.0f, long_width: %.0f, reset_limit: %.0f, sync_width: %.0f\n",
                device.short_width, device.long_width,
                device.reset_limit, device.sync_width);
        switch (device.modulation) {
        case FSK_PULSE_PCM:
            fprintf(stderr, "Use a flex decoder with -X 'n=name,m=FSK_PCM,s=%.0f,l=%.0f,r=%.0f'\n",
                    device.short_width, device.long_width, device.reset_limit);
            pulse_slicer_pcm(data, &device);
            break;
        case OOK_PULSE_PPM:
            fprintf(stderr, "Use a flex decoder with -X 'n=name,m=OOK_PPM,s=%.0f,l=%.0f,g=%.0f,r=%.0f'\n",
                    device.short_width, device.long_width,
                    device.gap_limit, device.reset_limit);
            data->gap[data->num_pulses - 1] = device.reset_limit / to_us + 1; // Be sure to terminate package
            pulse_slicer_ppm(data, &device);
            break;
        case OOK_PULSE_PWM:
            fprintf(stderr, "Use a flex decoder with -X 'n=name,m=OOK_PWM,s=%.0f,l=%.0f,r=%.0f,g=%.0f,t=%.0f,y=%.0f'\n",
                    device.short_width, device.long_width, device.reset_limit,
                    device.gap_limit, device.tolerance, device.sync_width);
            data->gap[data->num_pulses - 1] = device.reset_limit / to_us + 1; // Be sure to terminate package
            pulse_slicer_pwm(data, &device);
            break;
        case FSK_PULSE_PWM:
            fprintf(stderr, "Use a flex decoder with -X 'n=name,m=FSK_PWM,s=%.0f,l=%.0f,r=%.0f,g=%.0f,t=%.0f,y=%.0f'\n",
                    device.short_width, device.long_width, device.reset_limit,
                    device.gap_limit, device.tolerance, device.sync_width);
            data->gap[data->num_pulses - 1] = device.reset_limit / to_us + 1; // Be sure to terminate package
            pulse_slicer_pwm(data, &device);
            break;
        case OOK_PULSE_MANCHESTER_ZEROBIT:
            fprintf(stderr, "Use a flex decoder with -X 'n=name,m=OOK_MC_ZEROBIT,s=%.0f,l=%.0f,r=%.0f'\n",
                    device.short_width, device.long_width, device.reset_limit);
            data->gap[data->num_pulses - 1] = device.reset_limit / to_us + 1; // Be sure to terminate package
            pulse_slicer_manchester_zerobit(data, &device);
            break;
        default:
            fprintf(stderr, "Unsupported\n");
        }
    }

    fprintf(stderr, "\n");
}
