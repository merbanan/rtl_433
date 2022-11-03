/** @file
    Pulse detect functions, FSK pulse detector.

    Copyright (C) 2015 Tommy Vestermark
    Copyright (C) 2019 Benjamin Larsson.
    Copyright (C) 2022 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_PULSE_DETECT_FSK_H_
#define INCLUDE_PULSE_DETECT_FSK_H_

#include "pulse_data.h"
#include <stdint.h>

/// State data for pulse_detect_fsk_ functions.
///
/// This should be private/opaque but the OOK pulse_detect uses this.
typedef struct {
    unsigned int fsk_pulse_length; ///< Counter for internal FSK pulse detection
    enum {
        PD_FSK_STATE_INIT  = 0, ///< Initial frequency estimation
        PD_FSK_STATE_FH    = 1, ///< High frequency (pulse)
        PD_FSK_STATE_FL    = 2, ///< Low frequency (gap)
        PD_FSK_STATE_ERROR = 3  ///< Error - stay here until cleared
    } fsk_state;

    int fm_f1_est; ///< Estimate for the F1 frequency for FSK
    int fm_f2_est; ///< Estimate for the F2 frequency for FSK

    int16_t var_test_max;
    int16_t var_test_min;
    int16_t maxx;
    int16_t minn;
    int16_t midd;
    int skip_samples;
} pulse_detect_fsk_t;

/// Init/clear Demodulate Frequency Shift Keying (FSK) state.
///
/// @param s Internal state
void pulse_detect_fsk_init(pulse_detect_fsk_t *s);

/// Demodulate Frequency Shift Keying (FSK) sample by sample.
///
/// Function is stateful between calls
/// Builds estimate for initial frequency. When frequency deviates more than a
/// threshold value it will determine whether the deviation is positive or negative
/// to classify it as a pulse or gap. It will then transition to other state (F1 or F2)
/// and build an estimate of the other frequency. It will then transition back and forth when current
/// frequency is closer to other frequency estimate.
/// Includes spurious suppression by coalescing pulses when pulse/gap widths are too short.
/// Pulses equal higher frequency (F1) and Gaps equal lower frequency (F2)
/// @param s Internal state
/// @param fm_n One single sample of FM data
/// @param fsk_pulses Will return a pulse_data_t structure for FSK demodulated data
void pulse_detect_fsk_classic(pulse_detect_fsk_t *s, int16_t fm_n, pulse_data_t *fsk_pulses);

/// Wrap up FSK modulation and store last data at End Of Package.
///
/// @param s Internal state
/// @param fsk_pulses Pulse_data_t structure for FSK demodulated data
void pulse_detect_fsk_wrap_up(pulse_detect_fsk_t *s, pulse_data_t *fsk_pulses);

/// Demodulate Frequency Shift Keying (FSK) sample by sample.
///
/// Function is stateful between calls
/// @param s Internal state
/// @param fm_n One single sample of FM data
/// @param fsk_pulses Will return a pulse_data_t structure for FSK demodulated data
void pulse_detect_fsk_minmax(pulse_detect_fsk_t *s, int16_t fm_n, pulse_data_t *fsk_pulses);

#endif /* INCLUDE_PULSE_DETECT_FSK_H_ */
