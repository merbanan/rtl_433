/** @file
    Pulse detection functions, FSK pulse detector.

    Copyright (C) 2015 Tommy Vestermark
    Copyright (C) 2019 Benjamin Larsson.
    Copyright (C) 2022 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "pulse_detect_fsk.h"
#include "c_util.h" // for MIN(), MAX()
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FSK adaptive frequency estimator constants
#define FSK_DEFAULT_FM_DELTA 6000       // Default estimate for frequency delta
#define FSK_EST_SLOW        64          // Constant for slowness of FSK estimators
#define FSK_EST_FAST        16          // Constant for slowness of FSK estimators
#define FSK_EST_ALT         128         // Constant for alternate FSK estimator modification scaling
/*
8 -> 19
16 -> 11
32 -> 5  tests/bresser_7in1/02/g005_868.3M_1000k.cu8 tests/lacrosse_ltv/BreezePro_LTV-WSDTH01/g001_914.938M_2400k.cu8 tests/lacrosse_ltv/LTV-TH2/g002_915M_1000k.cu8
64 -> 1  tests/bresser_7in1/02/g005_868.3M_1000k.cu8
128 -> 0
*/

void pulse_detect_fsk_init(pulse_detect_fsk_t *s)
{
    *s              = (pulse_detect_fsk_t){0};
    s->var_test_max = INT16_MIN;
    s->var_test_min = INT16_MAX;
    s->skip_samples = 40;
}

void pulse_detect_fsk_classic(pulse_detect_fsk_t *s, int16_t fm_n, pulse_data_t *fsk_pulses)
{
    int const fm_f1_delta = abs(fm_n - s->fm_f1_est); // Get delta from F1 frequency estimate
    int const fm_f2_delta = abs(fm_n - s->fm_f2_est); // Get delta from F2 frequency estimate
    s->fsk_pulse_length += 1;

//    static unsigned counter = 0;
//    fprintf(stderr, "STATE[%06u] %d N %d, F1 %d (d %d), F2 %d (d %d)\n", ++counter, s->fsk_state, fm_n, s->fm_f1_est, fm_f1_delta, s->fm_f2_est, fm_f2_delta);

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
                    s->fsk_state = PD_FSK_STATE_FH;
                    s->fm_f2_est = s->fm_f1_est;    // Switch estimates
                    s->fm_f1_est = fm_n;            // Prime F1 estimate
                    fsk_pulses->pulse[0] = 0;        // Initial frequency was a gap...
                    fsk_pulses->gap[0] = s->fsk_pulse_length;        // Store gap width
                    fsk_pulses->num_pulses += 1;
                    s->fsk_pulse_length = 0;
                }
                // Negative Frequency delta - Initial frequency was high (pulse)
                else {
                    s->fsk_state = PD_FSK_STATE_FL;
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
        case PD_FSK_STATE_FH:        // Pulse high at F1 frequency
            // Closer to F2 than F1?
            if (fm_f1_delta > fm_f2_delta) {
                s->fsk_state = PD_FSK_STATE_FL;
                // Store if pulse is not too short (suppress spurious)
                if (s->fsk_pulse_length >= PD_MIN_PULSE_SAMPLES) {
                    fsk_pulses->pulse[fsk_pulses->num_pulses] = s->fsk_pulse_length;    // Store pulse width
                    s->fsk_pulse_length = 0;
                }
                // Else rewind to last gap
                else {
                    s->fsk_pulse_length += fsk_pulses->gap[fsk_pulses->num_pulses-1];    // Restore counter
                    fsk_pulses->num_pulses -= 1;        // Rewind one pulse
                    // Are we back to initial frequency? (Was initial frequency a gap?)
                    if ((fsk_pulses->num_pulses == 0) && (fsk_pulses->pulse[0] == 0)) {
                        s->fm_f1_est = s->fm_f2_est;    // Switch back estimates
                        s->fsk_state = PD_FSK_STATE_INIT;
                    }
                }
            }
            // Still below threshold
            else {
                if (fm_n > s->fm_f1_est) {
                    s->fm_f1_est += fm_n/FSK_EST_FAST - s->fm_f1_est/FSK_EST_FAST;    // Fast estimator
                } else {
                    s->fm_f1_est += fm_n/FSK_EST_SLOW - s->fm_f1_est/FSK_EST_SLOW;    // Slow estimator
                }
                // also pull other estimator very slowly
                if (fm_n < s->fm_f2_est) {
                    s->fm_f2_est += fm_n / (FSK_EST_FAST * FSK_EST_ALT) - s->fm_f2_est / (FSK_EST_FAST * FSK_EST_ALT); // Fast estimator
                }
                else {
                    s->fm_f2_est += fm_n / (FSK_EST_SLOW * FSK_EST_ALT) - s->fm_f2_est / (FSK_EST_SLOW * FSK_EST_ALT); // Slow estimator
                }
            }
            break;
        case PD_FSK_STATE_FL:        // Pulse gap at F2 frequency
            // Freq closer to F1 than F2 ?
            if (fm_f2_delta > fm_f1_delta) {
                s->fsk_state = PD_FSK_STATE_FH;
                // Store if pulse is not too short (suppress spurious)
                if (s->fsk_pulse_length >= PD_MIN_PULSE_SAMPLES) {
                    fsk_pulses->gap[fsk_pulses->num_pulses] = s->fsk_pulse_length;    // Store gap width
                    fsk_pulses->num_pulses += 1;    // Go to next pulse
                    s->fsk_pulse_length = 0;
                    // When pulse buffer is full go to error state
                    if (fsk_pulses->num_pulses >= PD_MAX_PULSES) {
                        //fprintf(stderr, "pulse_detect_fsk_classic(): Maximum number of pulses reached!\n");
                        //s->fsk_state = PD_FSK_STATE_ERROR;
                        // TODO: workaround, specifically for the Inkbird-ITH20R: free some of the buffer
                        pulse_data_shift(fsk_pulses);
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
                if (fm_n < s->fm_f2_est) {
                    s->fm_f2_est += fm_n/FSK_EST_FAST - s->fm_f2_est/FSK_EST_FAST;    // Fast estimator
                } else {
                    s->fm_f2_est += fm_n/FSK_EST_SLOW - s->fm_f2_est/FSK_EST_SLOW;    // Slow estimator
                }

                // also pull other estimator very slowly
                if (fm_n > s->fm_f1_est) {
                    s->fm_f1_est += fm_n / (FSK_EST_FAST * FSK_EST_ALT) - s->fm_f1_est / (FSK_EST_FAST * FSK_EST_ALT); // Fast estimator
                }
                else {
                    s->fm_f1_est += fm_n / (FSK_EST_SLOW * FSK_EST_ALT) - s->fm_f1_est / (FSK_EST_SLOW * FSK_EST_ALT); // Slow estimator
                }
            }
            break;
        case PD_FSK_STATE_ERROR:        // Stay here until cleared
            break;
        default:
            fprintf(stderr, "pulse_detect_fsk_classic(): Unknown FSK state!!\n");
            s->fsk_state = PD_FSK_STATE_ERROR;
    } // switch(s->fsk_state)
}

void pulse_detect_fsk_wrap_up(pulse_detect_fsk_t *s, pulse_data_t *fsk_pulses)
{
    if (fsk_pulses->num_pulses < PD_MAX_PULSES) { // Avoid overflow
        s->fsk_pulse_length += 1;
        if (s->fsk_state == PD_FSK_STATE_FH) {
            fsk_pulses->pulse[fsk_pulses->num_pulses] = s->fsk_pulse_length; // Store last pulse
            fsk_pulses->gap[fsk_pulses->num_pulses]   = 0;                   // Zero gap at end
        }
        else {
            fsk_pulses->gap[fsk_pulses->num_pulses] = s->fsk_pulse_length; // Store last gap
        }
        fsk_pulses->num_pulses += 1;
    }
}

void pulse_detect_fsk_minmax(pulse_detect_fsk_t *s, int16_t fm_n, pulse_data_t *fsk_pulses)
{
    int16_t mid = 0;

    /* Skip a few samples in the beginning, need for framing
     * otherwise the min/max trackers won't converge properly
     */
    if (!s->skip_samples) {
        s->var_test_max = MAX(fm_n, s->var_test_max);
        s->var_test_min = MIN(fm_n, s->var_test_min);
        mid = (s->var_test_max + s->var_test_min) / 2;
        if (fm_n > mid) {
            s->var_test_max -= 10;
        }
        if (fm_n < mid) {
            s->var_test_min += 10;
        }

        s->fsk_pulse_length += 1;
        switch(s->fsk_state) {
            case PD_FSK_STATE_INIT:
                if (fm_n > mid) {
                    s->fsk_state = PD_FSK_STATE_FH;
                }
                if (fm_n <= mid) {
                    s->fsk_state = PD_FSK_STATE_FL;
                }
                break;
            case PD_FSK_STATE_FH:
                if (fm_n < mid) {
                    s->fsk_state = PD_FSK_STATE_FL;
                    fsk_pulses->pulse[fsk_pulses->num_pulses] = s->fsk_pulse_length;
                    s->fsk_pulse_length = 0;
                }
                s->fm_f2_est += fm_n / FSK_EST_SLOW - s->fm_f2_est / FSK_EST_SLOW; // Slow estimator
                break;
            case PD_FSK_STATE_FL:
                if (fm_n > mid) {
                    s->fsk_state = PD_FSK_STATE_FH;
                    fsk_pulses->gap[fsk_pulses->num_pulses] = s->fsk_pulse_length;
                    fsk_pulses->num_pulses += 1;
                    s->fsk_pulse_length = 0;
                    // When pulse buffer is full go to error state
                    if (fsk_pulses->num_pulses >= PD_MAX_PULSES) {
                        //fprintf(stderr, "pulse_detect_fsk_minmax(): Maximum number of pulses reached!\n");
                        //s->fsk_state = PD_FSK_STATE_ERROR;
                        // TODO: workaround, specifically for the Inkbird-ITH20R: free some of the buffer
                        pulse_data_shift(fsk_pulses);
                    }
                }
                s->fm_f1_est += fm_n / FSK_EST_SLOW - s->fm_f1_est / FSK_EST_SLOW; // Slow estimator
                break;
            case PD_FSK_STATE_ERROR:        // Stay here until cleared
                break;
            default:
                fprintf(stderr, "pulse_detect_fsk_minmax(): Unknown FSK state!!\n");
                s->fsk_state = PD_FSK_STATE_ERROR;
                break;
        }
    }
    if (s->skip_samples > 0) {
        s->skip_samples -= 1;
    }
}

/*
Gaussian mixture model with 2 components
https://scikit-learn.org/stable/modules/mixture.html

Optimal dichotomization of bimodal Gaussian mixtures

https://github.com/Ransaka/GMM-from-scratch

https://towardsdatascience.com/gaussian-mixture-models-gmms-from-theory-to-implementation-4406c7fe9847/
*/
static void pulse_detect_fsk_avg(pulse_detect_fsk_t *s, int16_t const *fm_data, unsigned fsk_start, unsigned fsk_end)
{
    (void)s;

    if (fsk_start >= fsk_end) {
        return;
    }

    // Pass 1: Calculate common average
    int32_t com_avg = 0; // Note: can sum up t0 2^16 samples
    int32_t com_cnt = fsk_end - fsk_start;
    for (unsigned j = fsk_start; j < fsk_end; ++j) {
        int16_t fm_n = fm_data[j];
        com_avg += fm_n;
        // fprintf(stderr, "STATE %d N -> %d\n", fm_n, com_avg);
    }
    com_avg = com_avg / com_cnt;

    // fprintf(stderr, "pulse_detect_fsk_avg com_avg: %d\n", com_avg);

    // Pass 2: Split at common average and calculate low and high average
    int32_t lo_avg = 0; // Note: can sum up t0 2^16 samples
    int32_t hi_avg = 0; // Note: can sum up t0 2^16 samples
    int32_t lo_cnt = 0;
    int32_t hi_cnt = 0;
    uint32_t lo_err = 0;
    uint32_t hi_err = 0;
    for (unsigned j = fsk_start; j < fsk_end; ++j) {
        int16_t fm_n = fm_data[j];
        if (fm_n > com_avg) {
            hi_avg += fm_n;
            hi_cnt += 1;
            hi_err += (fm_n - com_avg) * (fm_n - com_avg);
        }
        else {
            lo_avg += fm_n;
            lo_cnt += 1;
            lo_err += (fm_n - com_avg) * (fm_n - com_avg);
        }
    }
    lo_avg = lo_avg / lo_cnt;
    hi_avg = hi_avg / hi_cnt;
    lo_err = lo_err / lo_cnt;
    hi_err = hi_err / hi_cnt;

    fprintf(stderr, "pulse_detect_fsk_avg com_avg: %d (%d) lo_avg: %d %d (%d) hi_avg: %d %d (%d)\n", com_avg, com_cnt, lo_avg, lo_err, lo_cnt, hi_avg, hi_err, hi_cnt);

    // Pass 3: Split at exact mid point and recalculate low and high average
    int32_t mid = lo_avg / 2 + hi_avg / 2;
    lo_avg = 0; // Note: can sum up t0 2^16 samples
    hi_avg = 0; // Note: can sum up t0 2^16 samples
    lo_cnt = 0;
    hi_cnt = 0;
    lo_err = 0;
    hi_err = 0;
    for (unsigned j = fsk_start; j < fsk_end; ++j) {
        int16_t fm_n = fm_data[j];
        if (fm_n > mid) {
            hi_avg += fm_n;
            hi_cnt += 1;
            hi_err += (fm_n - com_avg) * (fm_n - com_avg);
        }
        else {
            lo_avg += fm_n;
            lo_cnt += 1;
            lo_err += (fm_n - com_avg) * (fm_n - com_avg);
        }
    }
    lo_avg = lo_avg / lo_cnt;
    hi_avg = hi_avg / hi_cnt;
    lo_err = lo_err / lo_cnt;
    hi_err = hi_err / hi_cnt;

    fprintf(stderr, "pulse_detect_fsk_avg com_avg: %d (%d) lo_avg: %d %d (%d) hi_avg: %d %d (%d)  mid %d\n", com_avg, com_cnt, lo_avg, lo_err, lo_cnt, hi_avg, hi_err, hi_cnt, mid);
}

static int pulse_detect_fsk_package_internal(pulse_detect_fsk_t *pulse_detect_fsk, int16_t const *fm_data, unsigned fsk_start, unsigned fsk_end, pulse_data_t *fsk_pulses, unsigned fpdm)
{
    fprintf(stderr, "pulse_detect_fsk_package PROCESSING %u at %u - %u\n", pulse_detect_fsk->pulse_done, fsk_start, fsk_end);

    // TESTS
    pulse_detect_fsk_avg(pulse_detect_fsk, fm_data, fsk_start, fsk_end);

    // FSK Demodulation
    if (fpdm == FSK_PULSE_DETECT_OLD) {
        // FIXME: pull the loop into the fsk detectors
        // FIXME: converging takes too long with a broken start of signal s.a. lacrosse_ltv/LTV-R3/g012_868.3M_1024k.cu8
/*
        // Skip a few initial samples, run for a limited number of samples to settle, then reset an run complete
        unsigned fsk_start_test = MIN(fsk_start + 100, fsk_end);
        unsigned fsk_end_test   = MIN(fsk_start + 1000, fsk_end);
        for (unsigned j = fsk_start_test; j < fsk_end_test; ++j) {
            pulse_detect_fsk_classic(pulse_detect_fsk, fm_data[j], fsk_pulses);
        }
        //pulse_data_clear(fsk_pulses); // too harsh
        fsk_pulses->num_pulses = 0;
        pulse_detect_fsk->fsk_state = 0;
        fprintf(stderr, "pulse_detect_fsk_package PROCESSING2 %u at %u - %u\n", pulse_detect_fsk->pulse_done, fsk_start, fsk_end);
*/

        for (unsigned j = fsk_start; j < fsk_end; ++j) {
            pulse_detect_fsk_classic(pulse_detect_fsk, fm_data[j], fsk_pulses);
        }
    }
    else {
        // FIXME: pull the loop into the fsk detectors
        for (unsigned j = fsk_start; j < fsk_end; ++j) {
            pulse_detect_fsk_minmax(pulse_detect_fsk, fm_data[j], fsk_pulses);
        }
    }

    fprintf(stderr, "pulse_detect_fsk_package GOT %u\n", fsk_pulses->num_pulses);
    // Determine if FSK modulation is detected
    if (fsk_pulses->num_pulses > PD_MIN_PULSES) {
        // Store last pulse/gap
        if (fpdm == FSK_PULSE_DETECT_OLD) {
            pulse_detect_fsk_wrap_up(pulse_detect_fsk, fsk_pulses);
        }
        // Store estimates
        fsk_pulses->fsk_f1_est = pulse_detect_fsk->fm_f1_est;
        fsk_pulses->fsk_f2_est = pulse_detect_fsk->fm_f2_est;
        // fsk_pulses->ook_low_estimate  = ook_pulses->ook_low_estimate;
        // fsk_pulses->ook_high_estimate = ook_pulses->ook_high_estimate;
        // fsk_pulses->end_ago           = ook_pulses->start_ago - ook_pulses->pulse[0];

        // pulse_detect_fsk_init(pulse_detect_fsk); // FIXME: mock to flag that we are done

        fprintf(stderr, "return PULSE_DATA_FSK %u\n", fsk_pulses->num_pulses);
        return 2; // package_type = PULSE_DATA_FSK;
    }

    return 0;
}

// detect if this is a new ook paket
// if so flush and clear the fsk
// run detect on all pulses and note the last processed pulse
// Run FSK demod if needed
// FIXME: decide if we need a FSK demod
// return 1 while an fsk packet is found, otherwise 0 if all pulses are processed
int pulse_detect_fsk_package(pulse_detect_fsk_t *pulse_detect_fsk, int16_t const *fm_data, unsigned n_samples, pulse_data_t const *ook_pulses, pulse_data_t *fsk_pulses, unsigned fpdm)
{
    // check for a partial trailing pulse
    int has_partial_gap = 0;
    int has_partial_pulse = 0;
    if (ook_pulses->num_pulses < PD_MAX_PULSES) {
        if (ook_pulses->gap[ook_pulses->num_pulses] > 0) {
            has_partial_gap = 1;
        }
        else if (ook_pulses->pulse[ook_pulses->num_pulses] > 0) {
            has_partial_pulse = 1;
        }
    }

    if (ook_pulses->serialno != pulse_detect_fsk->curr_serial) {
        fprintf(stderr, "--> New Serial, now %u, was %u, processing %u pulses (+%dG +%dP)\n",
                ook_pulses->serialno, pulse_detect_fsk->curr_serial, ook_pulses->num_pulses, has_partial_gap, has_partial_pulse);
        // run from pulse 0 to num_pulses plus partial gap pulse or partial pulse
        pulse_detect_fsk->pulse_done = 0;
        // clear the fsk pulses
    }
    else {
        fprintf(stderr, "--> Same Serial %u, continuing at %u of now %u pulses (+%dG +%dP)\n",
                pulse_detect_fsk->curr_serial, pulse_detect_fsk->pulse_done, ook_pulses->num_pulses, has_partial_gap, has_partial_pulse);
        // sanity check
        if (pulse_detect_fsk->pulse_done > ook_pulses->num_pulses) {
            fprintf(stderr, "NOTE: we did process a partial pulse already\n");
        }
        if (pulse_detect_fsk->pulse_done > ook_pulses->num_pulses + 1) {
            fprintf(stderr, "THIS IS A BUG: pulse_detect_fsk_package ERROR\n");
            exit(-1);
        }

        // run from pulse pulse_done to num_pulses plus partial gap pulse or partial pulse
    }
    pulse_detect_fsk->curr_serial = ook_pulses->serialno;

    // ago times should already be aged or zero if this is a fresh package.
    /*
    if (fsk_pulses->start_ago > 0 && fsk_pulses->start_ago <= n_samples) {
        fprintf(stderr, "SUPERSEDED: pulse_detect_fsk_package: fsk_pulses->start_ago %u is not aged!\n", fsk_pulses->start_ago);
    }
    if (fsk_pulses->end_ago > 0 && fsk_pulses->end_ago <= n_samples) {
        fprintf(stderr, "SUPERSEDED: pulse_detect_fsk_package: fsk_pulses->end_ago %u is not aged!\n", fsk_pulses->end_ago);
    }
    */

    // Special case: continue a partial pulse, do this without resetting fsk_pulses

    // If the ook_pulses are the same (serialno) and the pulse length at pulse_done increased
    if (ook_pulses->serialno == pulse_detect_fsk->curr_serial
            && pulse_detect_fsk->pulse_done > 0 // this is implied by partial_pulse > 0
            && pulse_detect_fsk->partial_pulse > 0
            && ook_pulses->num_pulses + 1 >= pulse_detect_fsk->pulse_done
            && pulse_detect_fsk->partial_pulse < (unsigned)ook_pulses->pulse[pulse_detect_fsk->pulse_done - 1]) {

        fprintf(stderr, "IMPORTANT: pulse_detect_fsk_package: continue a partial pulse %u==%u, %u done <= %u pulses now, partial %u, now %d length\n",
                ook_pulses->serialno, pulse_detect_fsk->curr_serial, pulse_detect_fsk->pulse_done, ook_pulses->num_pulses, pulse_detect_fsk->partial_pulse, ook_pulses->pulse[pulse_detect_fsk->pulse_done - 1]);

        unsigned fsk_start = 0;
        unsigned pulse_length = (unsigned)ook_pulses->pulse[pulse_detect_fsk->pulse_done - 1];
        unsigned fsk_end = pulse_length - pulse_detect_fsk->partial_pulse;
        pulse_detect_fsk->partial_pulse = (unsigned)ook_pulses->pulse[pulse_detect_fsk->pulse_done - 1];

        if (pulse_detect_fsk_package_internal(pulse_detect_fsk, fm_data, fsk_start, fsk_end, fsk_pulses, fpdm)) {
            if (ook_pulses->gap[pulse_detect_fsk->pulse_done - 1] > 0) {
                return 1; // complete pulse
            }
            else {
                return 0; // still partial
            }
        }

        // FIXME: run and return state

        /*
        // Find bounds of partial pulse
        // need to calc the added length of the first pulse...
        unsigned already_proccessed = fsk_pulses->start_ago - fsk_pulses->end_ago;
        fsk_end   = fsk_start + ook_pulses->pulse[0] - fsk_pulses->start_ago + fsk_pulses->end_ago;
        //fsk_end -= already_proccessed;
        fsk_pulses->end_ago = fsk_pulses->start_ago - ook_pulses->pulse[0];
        // -vs-
        // fsk_pulses->end_ago = ook_pulses->start_ago - ook_pulses->pulse[0];
        */
    }

    // Regular case: process all complete pulses
    // and
    // Special case: process a trailing pulse with partial gap

    while (pulse_detect_fsk->pulse_done < ook_pulses->num_pulses + has_partial_gap) {
        // Reset demod, keep meta-state
        unsigned curr_serial = pulse_detect_fsk->curr_serial;
        unsigned pulse_done = pulse_detect_fsk->pulse_done;
        unsigned partial_pulse = pulse_detect_fsk->partial_pulse;
        pulse_detect_fsk_init(pulse_detect_fsk);
        pulse_detect_fsk->curr_serial = curr_serial;
        pulse_detect_fsk->pulse_done = pulse_done;
        pulse_detect_fsk->partial_pulse = partial_pulse;

        // Initialize all pulses data
        pulse_data_clear(fsk_pulses);
        fsk_pulses->sample_rate = ook_pulses->sample_rate;
        // fsk_pulses->offset      = demod->input_pos + n_samples - ook_pulses->start_ago;
        fsk_pulses->offset    = ook_pulses->offset;
        fsk_pulses->start_ago = ook_pulses->start_ago;
        fsk_pulses->end_ago = fsk_pulses->start_ago; // nothing processed so far
        // pulse_detect_fsk_init(pulse_detect_fsk);

        fsk_pulses->ook_low_estimate  = ook_pulses->ook_low_estimate;
        fsk_pulses->ook_high_estimate = ook_pulses->ook_high_estimate;
        //fsk_pulses->end_ago           = ook_pulses->start_ago - ook_pulses->pulse[0];

        // run demod here

        // calculate offset for pulse N
        unsigned pulse_offset = 0;
        for (unsigned j = 0; j < pulse_detect_fsk->pulse_done; ++j) {
            pulse_offset += (unsigned)ook_pulses->pulse[j] + (unsigned)ook_pulses->gap[j];
        }
        unsigned pulse_length = (unsigned)ook_pulses->pulse[pulse_detect_fsk->pulse_done];

        // the first sample in this frame is n_samples old,
        // sanity check that ook_pulses->start_ago + fsk_start is newer than is n_samples old, i.e. contained in this frame

//        fprintf(stderr, "pulse %u, start_ago %u, pulse_offset %u, length %u, n_samples %u\n", pulse_detect_fsk->pulse_done, ook_pulses->start_ago, pulse_offset, pulse_length, n_samples);

        // step forward
        pulse_detect_fsk->pulse_done += 1;

        if (ook_pulses->start_ago <= pulse_offset) {
            fprintf(stderr, "THIS IS A BUG: ook_pulses->start_ago <= pulse_offset\n");
            continue;
        }
        unsigned pulse_start_ago = ook_pulses->start_ago - pulse_offset;
        if (pulse_start_ago >= n_samples) {
            fprintf(stderr, "THIS IS A BUG: pulse_start_ago >= n_samples\n");
            continue;
        }

        unsigned fsk_start = n_samples - pulse_start_ago;
        unsigned fsk_end   = fsk_start + pulse_length + 1;
        fsk_pulses->start_ago         = pulse_start_ago;
        fsk_pulses->end_ago           = pulse_start_ago - pulse_length;

        fsk_end   = MIN(fsk_end + 10, n_samples); // the gap_start leniency from ook detect
        if (pulse_detect_fsk_package_internal(pulse_detect_fsk, fm_data, fsk_start, fsk_end, fsk_pulses, fpdm)) {
            return 1;
        }

        /*
        if (!is_complete) {
            // Partial OOK package starting at ook_pulses->start_ago samples before end of the current frame
            // i.e. starting in a past frame if ook_pulses->start_ago is bigger than n_samples

            // FIXME: run the FSK demod if this a first pulse
    fprintf(stderr, "got PULSE_DATA_OOK_PARTIAL %u (%u to %u)\n", ook_pulses->num_pulses, ook_pulses->start_ago, ook_pulses->end_ago);
        }
        if (is_complete) {
            // Complete OOK package starting at ook_pulses->start_ago samples before end of the current frame
            // i.e. starting in a past frame if ook_pulses->start_ago is bigger than n_samples
            // and ending at ook_pulses->end_ago samples before end of the current frame

            // FIXME: run the FSK demod if this a first pulse
    fprintf(stderr, "got PULSE_DATA_OOK %u (%u to %u)\n", ook_pulses->num_pulses, ook_pulses->start_ago, ook_pulses->end_ago);
        */
    }

    /*
    // Special case: process a trailing pulse with partial gap
    if (has_partial_gap) {
        fprintf(stderr, "FIXME: Skippping trailing partial gap\n");
        pulse_detect_fsk->pulse_done += 1;
    }
    */

    // Special case: process a trailing partial pulse
    if (has_partial_pulse) {
        fprintf(stderr, "FIXME: Skippping trailing partial pulse\n");

        // FIXME: actually process partial pulse...

/* COPIED */
        // Reset demod, keep meta-state
        unsigned curr_serial   = pulse_detect_fsk->curr_serial;
        unsigned pulse_done    = pulse_detect_fsk->pulse_done;
        unsigned partial_pulse = pulse_detect_fsk->partial_pulse;
        pulse_detect_fsk_init(pulse_detect_fsk);
        pulse_detect_fsk->curr_serial   = curr_serial;
        pulse_detect_fsk->pulse_done    = pulse_done;
        pulse_detect_fsk->partial_pulse = partial_pulse;

        // Initialize all pulses data
        pulse_data_clear(fsk_pulses);
        fsk_pulses->sample_rate = ook_pulses->sample_rate;
        // fsk_pulses->offset      = demod->input_pos + n_samples - ook_pulses->start_ago;
        fsk_pulses->offset    = ook_pulses->offset;
        fsk_pulses->start_ago = ook_pulses->start_ago;
        fsk_pulses->end_ago   = fsk_pulses->start_ago; // nothing processed so far
        // pulse_detect_fsk_init(pulse_detect_fsk);

        fsk_pulses->ook_low_estimate  = ook_pulses->ook_low_estimate;
        fsk_pulses->ook_high_estimate = ook_pulses->ook_high_estimate;
        // fsk_pulses->end_ago           = ook_pulses->start_ago - ook_pulses->pulse[0];

        // run demod here

        // calculate offset for pulse N
        unsigned pulse_offset = 0;
        for (unsigned j = 0; j < pulse_detect_fsk->pulse_done; ++j) {
            pulse_offset += (unsigned)ook_pulses->pulse[j] + (unsigned)ook_pulses->gap[j];
        }
        unsigned pulse_length = (unsigned)ook_pulses->pulse[pulse_detect_fsk->pulse_done];

        // the first sample in this frame is n_samples old,
        // sanity check that ook_pulses->start_ago + fsk_start is newer than is n_samples old, i.e. contained in this frame

        //        fprintf(stderr, "pulse %u, start_ago %u, pulse_offset %u, length %u, n_samples %u\n", pulse_detect_fsk->pulse_done, ook_pulses->start_ago, pulse_offset, pulse_length, n_samples);

        // step forward
        pulse_detect_fsk->pulse_done += 1;

        if (ook_pulses->start_ago <= pulse_offset) {
            fprintf(stderr, "THIS IS A BUG: ook_pulses->start_ago <= pulse_offset\n");
        }
        unsigned pulse_start_ago = ook_pulses->start_ago - pulse_offset;
        if (pulse_start_ago >= n_samples) {
            fprintf(stderr, "THIS IS A BUG: pulse_start_ago >= n_samples\n");
        }

        unsigned fsk_start = n_samples - pulse_start_ago;
        unsigned fsk_end   = fsk_start + pulse_length + 1;
        fsk_pulses->start_ago = pulse_start_ago;
        fsk_pulses->end_ago   = pulse_start_ago - pulse_length;

        pulse_detect_fsk_package_internal(pulse_detect_fsk, fm_data, fsk_start, fsk_end, fsk_pulses, fpdm);
/* COPIED */

        pulse_detect_fsk->partial_pulse = (unsigned)ook_pulses->pulse[pulse_detect_fsk->pulse_done - 1];
    }

    return 0;
}

/*
FAIL tests/alecto_ws_1200/01/g001_433.92M_250k.cu8
FAIL rtl_433_tests/tests/fineoffset/fineoffset_wh65b/01/g001_915.05M_250k.cu8
 */