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
#include "util.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FSK adaptive frequency estimator constants
#define FSK_DEFAULT_FM_DELTA 6000       // Default estimate for frequency delta
#define FSK_EST_SLOW        64          // Constant for slowness of FSK estimators
#define FSK_EST_FAST        16          // Constant for slowness of FSK estimators

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
