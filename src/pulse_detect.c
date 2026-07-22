/** @file
    Pulse detection functions.

    Copyright (C) 2015 Tommy Vestermark
    Copyright (C) 2020 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "pulse_detect.h"
#include "pulse_detect_fsk.h"
#include "pulse_data.h"
#include "baseband.h"
#include "c_util.h" // for MIN(), MAX()
#include "logger.h"
#include "fatal.h"
#include <stdio.h>
#include <stdlib.h>

// OOK adaptive level estimator constants
#define OOK_MAX_HIGH_LEVEL  DB_TO_AMP(0)   // Maximum estimate for high level (-0 dB)
#define OOK_MAX_LOW_LEVEL   DB_TO_AMP(-15) // Maximum estimate for low level
#define OOK_EST_HIGH_RATIO  64          // Constant for slowness of OOK high level estimator
#define OOK_EST_LOW_RATIO   1024        // Constant for slowness of OOK low level (noise) estimator (very slow)

/// Internal state data for pulse_pulse_package()
struct pulse_detect {
    int use_mag_est;          ///< Whether the envelope data is an amplitude or magnitude.
    int ook_fixed_high_level; ///< Manual detection level override, 0 = auto.
    int ook_min_high_level;   ///< Minimum estimate of high level (-12 dB: 1000 amp, 4000 mag).
    int ook_high_low_ratio;   ///< Default ratio between high and low (noise) level (9 dB: x8 amp, 11 dB: x3.6 mag).

    enum {
        PD_OOK_STATE_IDLE      = 0,
        PD_OOK_STATE_PULSE     = 1,
        PD_OOK_STATE_GAP_START = 2,
        PD_OOK_STATE_GAP       = 3,
    } ook_state;
    int pulse_length; ///< Counter for internal pulse detection
    int max_pulse;    ///< Size of biggest pulse detected

    int data_counter;    ///< Counter for how much of data chunk is processed
    int lead_in_counter; ///< Counter for allowing initial noise estimate to settle

    int ook_low_estimate;  ///< Estimate for the OOK low level (base noise level) in the envelope data
    int ook_high_estimate; ///< Estimate for the OOK high level

    int verbosity; ///< Debug output verbosity, 0=None, 1=Levels, 2=Histograms

    pulse_detect_fsk_t pulse_detect_fsk[FSK_PULSE_DETECTOR_COUNT];
};

static int fsk_detector_enabled(unsigned fpdm, unsigned detector)
{
    if (fpdm == FSK_PULSE_DETECT_OLD) {
        return detector == FSK_PULSE_DETECTOR_CLASSIC;
    }
    if (fpdm == FSK_PULSE_DETECT_NEW) {
        return detector == FSK_PULSE_DETECTOR_MINMAX || detector == FSK_PULSE_DETECTOR_HYSTERESIS;
    }
    if (fpdm == FSK_PULSE_DETECT_HYSTERESIS) {
        return detector == FSK_PULSE_DETECTOR_HYSTERESIS;
    }
    if (fpdm == FSK_PULSE_DETECT_MEDIAN) {
        return detector == FSK_PULSE_DETECTOR_MEDIAN;
    }
    return fpdm == FSK_PULSE_DETECT_AUTO;
}

static void pulse_detect_fsk_init_all(pulse_detect_t *s)
{
    for (unsigned detector = 0; detector < FSK_PULSE_DETECTOR_COUNT; ++detector) {
        pulse_detect_fsk_init(&s->pulse_detect_fsk[detector]);
    }
}

static void pulse_detect_fsk_sample(pulse_detect_t *s, int16_t fm_n,
        pulse_data_t fsk_pulses[FSK_PULSE_DETECTOR_COUNT], unsigned fpdm)
{
    if (fsk_detector_enabled(fpdm, FSK_PULSE_DETECTOR_CLASSIC)) {
        pulse_detect_fsk_classic(&s->pulse_detect_fsk[FSK_PULSE_DETECTOR_CLASSIC], fm_n,
                &fsk_pulses[FSK_PULSE_DETECTOR_CLASSIC]);
    }
    if (fsk_detector_enabled(fpdm, FSK_PULSE_DETECTOR_MINMAX)) {
        pulse_detect_fsk_minmax(&s->pulse_detect_fsk[FSK_PULSE_DETECTOR_MINMAX], fm_n,
                &fsk_pulses[FSK_PULSE_DETECTOR_MINMAX]);
    }
    if (fsk_detector_enabled(fpdm, FSK_PULSE_DETECTOR_HYSTERESIS)) {
        pulse_detect_fsk_minmax_hysteresis(&s->pulse_detect_fsk[FSK_PULSE_DETECTOR_HYSTERESIS], fm_n,
                &fsk_pulses[FSK_PULSE_DETECTOR_HYSTERESIS]);
    }
    if (fsk_detector_enabled(fpdm, FSK_PULSE_DETECTOR_MEDIAN)) {
        pulse_detect_fsk_minmax_median(&s->pulse_detect_fsk[FSK_PULSE_DETECTOR_MEDIAN], fm_n,
                &fsk_pulses[FSK_PULSE_DETECTOR_MEDIAN]);
    }
}

static int pulse_detect_fsk_finalize(pulse_detect_t *s,
        pulse_data_t fsk_pulses[FSK_PULSE_DETECTOR_COUNT], unsigned fpdm, unsigned fsk_primary, unsigned end_ago)
{
    // Alternate min/max slicers can chatter on OOK pulses. They may supply a
    // better FSK decode, but only the legacy preferred slicer may classify it.
    if (!fsk_detector_enabled(fpdm, fsk_primary)
            || fsk_pulses[fsk_primary].num_pulses <= PD_MIN_PULSES) {
        return 0;
    }

    int fsk_detected = 0;

    for (unsigned detector = 0; detector < FSK_PULSE_DETECTOR_COUNT; ++detector) {
        if (!fsk_detector_enabled(fpdm, detector) || fsk_pulses[detector].num_pulses <= PD_MIN_PULSES) {
            continue;
        }
        if (detector == FSK_PULSE_DETECTOR_CLASSIC) {
            pulse_detect_fsk_wrap_up(&s->pulse_detect_fsk[detector], &fsk_pulses[detector]);
        }
        fsk_pulses[detector].fsk_f1_est        = s->pulse_detect_fsk[detector].fm_f1_est;
        fsk_pulses[detector].fsk_f2_est        = s->pulse_detect_fsk[detector].fm_f2_est;
        fsk_pulses[detector].ook_low_estimate  = s->ook_low_estimate;
        fsk_pulses[detector].ook_high_estimate = s->ook_high_estimate;
        fsk_pulses[detector].end_ago           = end_ago;
        fsk_detected                           = 1;
    }

    return fsk_detected;
}

pulse_detect_t *pulse_detect_create(void)
{
    pulse_detect_t *pulse_detect = calloc(1, sizeof(pulse_detect_t));
    if (!pulse_detect) {
        WARN_CALLOC("pulse_detect_create()");
        return NULL;
    }

    pulse_detect_set_levels(pulse_detect, 0, 0.0f, -12.1442f, 9.0f, 0);

    return pulse_detect;
}

void pulse_detect_free(pulse_detect_t *pulse_detect)
{
    free(pulse_detect);
}

void pulse_detect_reset(pulse_detect_t *pulse_detect)
{
    pulse_detect->ook_state         = PD_OOK_STATE_IDLE;
    pulse_detect->pulse_length      = 0;
    pulse_detect->max_pulse         = 0;
    pulse_detect->data_counter      = 0;
    pulse_detect->lead_in_counter   = 0;
    pulse_detect->ook_low_estimate  = 0;
    pulse_detect->ook_high_estimate = 0;
    pulse_detect_fsk_init_all(pulse_detect);
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
    for (int i = 0; i < 37; ++i) {
        fprintf(stderr, ">%3d dB: %5d smps\n", 3 - i, att_hist[i]);
    }
}

/// Demodulate On/Off Keying (OOK) and Frequency Shift Keying (FSK) from an envelope signal
static void report_segment(pulse_segment_fn segment_fn, void *context,
        int start, int end, uint64_t sample_offset, unsigned flags)
{
    if (segment_fn && start >= 0 && end >= start
            && (end > start || flags)) {
        segment_fn(context, (size_t)start, (size_t)(end - start),
                sample_offset + (uint64_t)start, flags);
    }
}

int pulse_detect_package(pulse_detect_t *pulse_detect,
        int16_t const *envelope_data, int16_t const *fm_data, int len,
        uint32_t samp_rate, uint64_t sample_offset, pulse_data_t *pulses,
        pulse_data_t fsk_pulses[FSK_PULSE_DETECTOR_COUNT], unsigned fpdm,
        unsigned fsk_primary, pulse_segment_fn segment_fn,
        void *segment_context)
{
    pulse_detect_t *s = pulse_detect;
    int segment_start = (s->ook_state == PD_OOK_STATE_PULSE
                    || s->ook_state == PD_OOK_STATE_GAP_START)
            && pulses->num_pulses == 0 ? s->data_counter : -1;
    unsigned segment_flags = 0;

    // Special case: flush any partial package
    if (len == 0) {

        if (segment_start >= 0) {
            report_segment(segment_fn, segment_context, 0, 0,
                    sample_offset, PULSE_SEGMENT_END);
        }

        switch (s->ook_state) {
        case PD_OOK_STATE_IDLE:
            break;
        case PD_OOK_STATE_PULSE:
            // Check for spurious short pulses
            if (s->pulse_length < PD_MIN_PULSE_SAMPLES) {
                if (pulses->num_pulses <= 1) {
                    // if this was the first pulse go back to idle
                    s->ook_state = PD_OOK_STATE_IDLE;
                    break;
                }
                else {
                    // otherwise emit a package, which then goes back to idle
                    s->ook_state = PD_OOK_STATE_GAP;
                }
            }
            else {
                // Continue with OOK decoding
                pulses->pulse[pulses->num_pulses] = s->pulse_length;                    // Store pulse width
                s->max_pulse                      = MAX(s->pulse_length, s->max_pulse); // Find largest pulse
                s->pulse_length                   = 0;
                s->ook_state                      = PD_OOK_STATE_GAP_START;
            }
            // intentional fallthrough
#if defined __has_attribute
#if __has_attribute(fallthrough)
            __attribute__((fallthrough));
#endif
#endif

        case PD_OOK_STATE_GAP_START:
            s->ook_state = PD_OOK_STATE_GAP;
            // Determine if FSK modulation is detected
            unsigned const end_ago = len - s->data_counter;
            if (pulse_detect_fsk_finalize(s, fsk_pulses, fpdm, fsk_primary, end_ago)) {
                pulses->end_ago = end_ago;
                s->ook_state                  = PD_OOK_STATE_IDLE; // Ensure everything is reset

                return PULSE_DATA_FSK;
            }

            // intentional fallthrough
#if defined __has_attribute
#if __has_attribute(fallthrough)
            __attribute__((fallthrough));
#endif
#endif

        case PD_OOK_STATE_GAP:
            pulses->gap[pulses->num_pulses] = s->pulse_length; // Store gap width
            pulses->num_pulses += 1;                           // Store last pulse
            s->ook_state = PD_OOK_STATE_IDLE;
            // Store estimates
            pulses->ook_low_estimate  = s->ook_low_estimate;
            pulses->ook_high_estimate = s->ook_high_estimate;
            pulses->end_ago           = len - s->data_counter;

            return PULSE_DATA_OOK;

        default:
            fprintf(stderr, "demod_OOK(): Unknown state!!\n");
            s->ook_state = PD_OOK_STATE_IDLE;
        }
    }

    int att_hist[37] = {0};
    int const samples_per_ms = samp_rate / 1000;

    s->ook_high_estimate = MAX(s->ook_high_estimate, pulse_detect->ook_min_high_level);    // Be sure to set initial minimum level

    if (s->data_counter == 0) {
        // age the pulse_data if this is a fresh buffer
        pulses->start_ago += len;
        for (unsigned detector = 0; detector < FSK_PULSE_DETECTOR_COUNT; ++detector) {
            fsk_pulses[detector].start_ago += len;
        }
    }

    int eop_on_spurious = 0;
    // Process all new samples
    while (s->data_counter < len) {
        // Calculate OOK detection threshold and hysteresis
        int16_t const am_n    = envelope_data[s->data_counter];
        if (pulse_detect->verbosity >= LOG_NOTICE) {
            int att = pulse_detect->use_mag_est ? mag_to_att(am_n) : amp_to_att(am_n);
            att_hist[att] += 1;
        }
        int16_t ook_threshold = (s->ook_low_estimate + MIN(s->ook_high_estimate, OOK_MAX_HIGH_LEVEL)) / 2;
        if (pulse_detect->ook_fixed_high_level != 0) {
            ook_threshold = pulse_detect->ook_fixed_high_level; // Manual override
        }
        int16_t const ook_hysteresis = ook_threshold / 8; // +-12%

        // OOK State machine
        switch (s->ook_state) {
            case PD_OOK_STATE_IDLE:
                if (am_n > (ook_threshold + ook_hysteresis)    // Above threshold?
                        && s->lead_in_counter > OOK_EST_LOW_RATIO) { // Lead in counter to stabilize noise estimate
                    // Initialize all data
                    pulse_data_clear(pulses);
                    pulses->sample_rate = samp_rate;
                    pulses->offset = sample_offset + s->data_counter;
                    pulses->start_ago = len - s->data_counter;
                    for (unsigned detector = 0; detector < FSK_PULSE_DETECTOR_COUNT; ++detector) {
                        pulse_data_clear(&fsk_pulses[detector]);
                        fsk_pulses[detector].sample_rate = samp_rate;
                        fsk_pulses[detector].offset      = sample_offset + s->data_counter;
                        fsk_pulses[detector].start_ago   = len - s->data_counter;
                    }
                    s->pulse_length = 0;
                    s->max_pulse = 0;
                    pulse_detect_fsk_init_all(s);
                    s->ook_state = PD_OOK_STATE_PULSE;
                    /* The FSK detectors start on the sample after the carrier
                       threshold crossing. Export that same discriminator range. */
                    segment_start = s->data_counter + 1;
                    segment_flags = PULSE_SEGMENT_START;
                }
                else {    // We are still idle..
                    // Estimate low (noise) level
                    int const ook_low_delta = am_n - s->ook_low_estimate;
                    s->ook_low_estimate += ook_low_delta / OOK_EST_LOW_RATIO;
                    s->ook_low_estimate += ((ook_low_delta > 0) ? 1 : -1);    // Hack to compensate for lack of fixed-point scaling
                    // Calculate default OOK high level estimate
                    s->ook_high_estimate = pulse_detect->ook_high_low_ratio * s->ook_low_estimate; // Default is a ratio of low level
                    s->ook_high_estimate = MAX(s->ook_high_estimate, pulse_detect->ook_min_high_level);
                    if (s->lead_in_counter <= OOK_EST_LOW_RATIO) s->lead_in_counter += 1;        // Allow initial estimate to settle
                }
                break;
            case PD_OOK_STATE_PULSE:
                s->pulse_length += 1;
                // End of pulse detected?
                if (am_n < (ook_threshold - ook_hysteresis)) {    // Gap?
                    // Check for spurious short pulses
                    if (s->pulse_length < PD_MIN_PULSE_SAMPLES) {
                        if (pulses->num_pulses <= 1) {
                            // if this was the first pulse go back to idle
                            s->ook_state = PD_OOK_STATE_IDLE;
                        } else {
                            // otherwise emit a package, which then goes back to idle
                            eop_on_spurious = 1;
                            s->ook_state = PD_OOK_STATE_GAP;
                        }
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
                    // Estimate pulse carrier frequency
                    pulses->fsk_f1_est += fm_data[s->data_counter] / OOK_EST_HIGH_RATIO - pulses->fsk_f1_est / OOK_EST_HIGH_RATIO;
                }
                // FSK Demodulation
                if (pulses->num_pulses == 0) {    // Only during first pulse
                    pulse_detect_fsk_sample(s, fm_data[s->data_counter], fsk_pulses, fpdm);
                }
                if (s->ook_state == PD_OOK_STATE_IDLE
                        && segment_start >= 0) {
                    report_segment(segment_fn, segment_context,
                            segment_start, s->data_counter + 1,
                            sample_offset,
                            segment_flags | PULSE_SEGMENT_END);
                    segment_start = -1;
                    segment_flags = 0;
                }
                break;
            case PD_OOK_STATE_GAP_START:    // Beginning of gap - it might be a spurious gap
                s->pulse_length += 1;
                // Pulse detected again already? (This is a spurious short gap)
                if (am_n > (ook_threshold + ook_hysteresis)) {    // New pulse?
                    s->pulse_length += pulses->pulse[pulses->num_pulses];    // Restore counter
                    s->ook_state = PD_OOK_STATE_PULSE;
                }
                // Or this gap is for real?
                else if (s->pulse_length >= PD_MIN_PULSE_SAMPLES) {
                    s->ook_state = PD_OOK_STATE_GAP;
                    // Determine if FSK modulation is detected
                    unsigned const end_ago = len - s->data_counter;
                    if (pulse_detect_fsk_finalize(s, fsk_pulses, fpdm, fsk_primary, end_ago)) {
                        pulses->end_ago = end_ago;
                        s->ook_state = PD_OOK_STATE_IDLE;    // Ensure everything is reset
                        report_segment(segment_fn, segment_context,
                                segment_start, s->data_counter,
                                sample_offset,
                                segment_flags | PULSE_SEGMENT_END);
                        if (pulse_detect->verbosity >= LOG_INFO) {
                            print_att_hist("PULSE_DATA_FSK", att_hist);
                        }
                        if (pulse_detect->verbosity >= LOG_NOTICE) {
                            fprintf(stderr, "Levels low: -%d dB  high: -%d dB  thres: -%d dB  hyst: (-%d to -%d dB)\n",
                                    mag_to_att(s->ook_low_estimate), mag_to_att(s->ook_high_estimate),
                                    mag_to_att(ook_threshold),
                                    mag_to_att(ook_threshold + ook_hysteresis),
                                    mag_to_att(ook_threshold - ook_hysteresis));
                        }
                        return PULSE_DATA_FSK;
                    }
                } // if
                // FSK Demodulation (continue during short gap - we might return...)
                if (pulses->num_pulses == 0) {    // Only during first pulse
                    pulse_detect_fsk_sample(s, fm_data[s->data_counter], fsk_pulses, fpdm);
                }
                if (s->ook_state == PD_OOK_STATE_GAP
                        && segment_start >= 0) {
                    report_segment(segment_fn, segment_context,
                            segment_start, s->data_counter + 1,
                            sample_offset,
                            segment_flags | PULSE_SEGMENT_END);
                    segment_start = -1;
                    segment_flags = 0;
                }
                break;
            case PD_OOK_STATE_GAP:
                s->pulse_length += 1;
                // New pulse detected?
                if (am_n > (ook_threshold + ook_hysteresis)) {    // New pulse?
                    pulses->gap[pulses->num_pulses] = s->pulse_length;    // Store gap width
                    pulses->num_pulses += 1;    // Next pulse

                    // EOP if too many pulses
                    if (pulses->num_pulses >= PD_MAX_PULSES) {
                        s->ook_state = PD_OOK_STATE_IDLE;
                        // Store estimates
                        pulses->ook_low_estimate = s->ook_low_estimate;
                        pulses->ook_high_estimate = s->ook_high_estimate;
                        pulses->end_ago = len - s->data_counter;
                        if (pulse_detect->verbosity >= LOG_INFO) {
                            print_att_hist("PULSE_DATA_OOK MAX_PULSES", att_hist);
                        }
                        return PULSE_DATA_OOK;    // End Of Package!!
                    }

                    s->pulse_length = 0;
                    s->ook_state = PD_OOK_STATE_PULSE;
                }

                // EOP if gap is too long
                if (eop_on_spurious
                        || (s->pulse_length > (PD_MAX_GAP_RATIO * s->max_pulse)    // gap/pulse ratio exceeded
                            && s->pulse_length > (PD_MIN_GAP_MS * samples_per_ms)) // Minimum gap exceeded
                        || s->pulse_length > (PD_MAX_GAP_MS * samples_per_ms)) {   // maximum gap exceeded
                    pulses->gap[pulses->num_pulses] = s->pulse_length;    // Store gap width
                    pulses->num_pulses += 1;    // Store last pulse
                    s->ook_state = PD_OOK_STATE_IDLE;
                    // Store estimates
                    pulses->ook_low_estimate = s->ook_low_estimate;
                    pulses->ook_high_estimate = s->ook_high_estimate;
                    pulses->end_ago = len - s->data_counter;
                    if (pulse_detect->verbosity >= LOG_INFO) {
                        print_att_hist("PULSE_DATA_OOK EOP", att_hist);
                    }
                    if (pulse_detect->verbosity >= LOG_NOTICE) {
                        fprintf(stderr, "Levels low: -%d dB  high: -%d dB  thres: -%d dB  hyst: (-%d to -%d dB)\n",
                                mag_to_att(s->ook_low_estimate), mag_to_att(s->ook_high_estimate),
                                mag_to_att(ook_threshold),
                                mag_to_att(ook_threshold + ook_hysteresis),
                                mag_to_att(ook_threshold - ook_hysteresis));
                    }
                    return PULSE_DATA_OOK;    // End Of Package!!
                }
                break;
            default:
                fprintf(stderr, "demod_OOK(): Unknown state!!\n");
                s->ook_state = PD_OOK_STATE_IDLE;
                if (segment_start >= 0) {
                    report_segment(segment_fn, segment_context,
                            segment_start, s->data_counter,
                            sample_offset,
                            segment_flags | PULSE_SEGMENT_END);
                    segment_start = -1;
                    segment_flags = 0;
                }
        } // switch
        s->data_counter += 1;
    } // while

    if (segment_start >= 0) {
        report_segment(segment_fn, segment_context, segment_start, len,
                sample_offset, segment_flags);
    }

    s->data_counter = 0;
    if (pulse_detect->verbosity >= LOG_DEBUG) {
        print_att_hist("Out of data", att_hist);
    }
    return 0;    // Out of data
}
