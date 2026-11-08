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
#include "pulse_detect.h"
#include <stdint.h>

/// FSK pulse detector mode to use.
enum {
    FSK_PULSE_DETECT_OLD,
    FSK_PULSE_DETECT_NEW,
    FSK_PULSE_DETECT_AUTO,
    FSK_PULSE_DETECT_HYSTERESIS,
    FSK_PULSE_DETECT_MEDIAN,
    FSK_PULSE_DETECT_END,
};

/// FSK pulse detectors.
enum {
    FSK_PULSE_DETECTOR_CLASSIC,    ///< Frequency estimate tracking
    FSK_PULSE_DETECTOR_MINMAX,     ///< Min/max midpoint slicer
    FSK_PULSE_DETECTOR_HYSTERESIS, ///< Min/max slicer with a wide hysteresis band
    FSK_PULSE_DETECTOR_MEDIAN,     ///< Min/max slicer on median filtered samples
    FSK_PULSE_DETECTOR_COUNT,
};

/// State data for pulse_detect_fsk_ functions.
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
    int restarted;        ///< Detection restarted on the second envelope pulse after a lead-in
    unsigned first_pulse; ///< Index of the envelope pulse detection started on

    int16_t median_samples[3]; ///< Median filter history
    unsigned median_count;     ///< Median filter history length
} pulse_detect_fsk_t;

/// Init/clear Demodulate Frequency Shift Keying (FSK) state.
///
/// @param s Internal state
void pulse_detect_fsk_init(pulse_detect_fsk_t *s);

/// Demodulate a buffer of Frequency Shift Keying (FSK) samples.
///
/// State and partial pulses are retained between buffers; len=0 is a no-op.
/// Builds estimate for initial frequency. When frequency deviates more than a
/// threshold value it will determine whether the deviation is positive or negative
/// to classify it as a pulse or gap. It will then transition to other state (F1 or F2)
/// and build an estimate of the other frequency. It will then transition back and forth when current
/// frequency is closer to other frequency estimate.
/// Includes spurious suppression by coalescing pulses when pulse/gap widths are too short.
/// Pulses equal higher frequency (F1) and Gaps equal lower frequency (F2)
/// @param s Internal state
/// @param fm_data Buffer of FM samples; may be NULL when len=0
/// @param len Number of samples in fm_data
/// @param fsk_pulses Will return a pulse_data_t structure for FSK demodulated data
void pulse_detect_fsk_classic(pulse_detect_fsk_t *s, int16_t const *fm_data, unsigned len, pulse_data_t *fsk_pulses);

/// Wrap up FSK modulation and store last data at End Of Package.
///
/// @param s Internal state
/// @param fsk_pulses Pulse_data_t structure for FSK demodulated data
void pulse_detect_fsk_wrap_up(pulse_detect_fsk_t *s, pulse_data_t *fsk_pulses);

/// Demodulate a buffer of Frequency Shift Keying (FSK) samples.
///
/// State and partial pulses are retained between buffers; len=0 is a no-op.
/// @param s Internal state
/// @param fm_data Buffer of FM samples; may be NULL when len=0
/// @param len Number of samples in fm_data
/// @param fsk_pulses Will return a pulse_data_t structure for FSK demodulated data
void pulse_detect_fsk_minmax(pulse_detect_fsk_t *s, int16_t const *fm_data, unsigned len, pulse_data_t *fsk_pulses);

/// Demodulate FSK using min/max tracking with a wide hysteresis band.
///
/// Complements the midpoint min/max detector on noisy signals.
/// State and partial pulses are retained between buffers; len=0 is a no-op.
/// @param s Internal state
/// @param fm_data Buffer of FM samples; may be NULL when len=0
/// @param len Number of samples in fm_data
/// @param fsk_pulses Will return a pulse_data_t structure for FSK demodulated data
void pulse_detect_fsk_minmax_hysteresis(pulse_detect_fsk_t *s, int16_t const *fm_data, unsigned len, pulse_data_t *fsk_pulses);

/// Demodulate FSK using min/max tracking on median filtered samples.
///
/// A median-of-three filter suppresses isolated FM spikes.
/// State and partial pulses are retained between buffers; len=0 is a no-op.
/// @param s Internal state
/// @param fm_data Buffer of FM samples; may be NULL when len=0
/// @param len Number of samples in fm_data
/// @param fsk_pulses Will return a pulse_data_t structure for FSK demodulated data
void pulse_detect_fsk_minmax_median(pulse_detect_fsk_t *s, int16_t const *fm_data, unsigned len, pulse_data_t *fsk_pulses);

/// Wrap up minmax FSK modulation and store last data at End Of Package.
///
/// Like pulse_detect_fsk_wrap_up() but drops a final F1 run shorter than
/// PD_MIN_PULSE_SAMPLES, the min/max tracker picks that up from noise after
/// the carrier ends.
///
/// @param s Internal state
/// @param fsk_pulses Pulse_data_t structure for FSK demodulated data
void pulse_detect_fsk_minmax_wrap_up(pulse_detect_fsk_t *s, pulse_data_t *fsk_pulses);

/// Consume one envelope event and its FM span. Returns 1 if a complete FSK
/// package is ready, otherwise 0. Partial spans retain state; only
/// PULSE_DETECT_PULSE can complete a package, which the caller then accepts with
/// pulse_detect_fsk_finalize(). A ready package left unaccepted is unchanged.
/// PULSE_DETECT_START resets the detector and pulse data,
/// PULSE_DETECT_RESTART and PULSE_DETECT_RETRY do the same starting at the new envelope pulse.
/// The caller ages fsk_pulses->start_ago once per input buffer.
///
/// @param s Internal state
/// @param fm_data Buffer of FM samples that span indexes into
/// @param span Newly consumed samples of the carrier pulse
/// @param event The enum pulse_detect_events value from pulse_detect_package()
/// @param pulses The OOK pulse_data_t structure of the envelope
/// @param[in,out] fsk_pulses Pulse_data_t structure for FSK demodulated data
/// @param detector One of FSK_PULSE_DETECTOR_*
/// @return 1 if a complete FSK package is ready, otherwise 0
int pulse_detect_fsk_package(pulse_detect_fsk_t *s, int16_t const *fm_data,
        pulse_detect_span_t const *span, int event, pulse_data_t const *pulses,
        pulse_data_t *fsk_pulses, unsigned detector);

/// Accept a ready FSK package: store the last pulse/gap and the estimates.
/// Call once, after pulse_detect_fsk_package() returned 1.
///
/// @param s Internal state
/// @param pulses The OOK pulse_data_t structure of the envelope, for the level and end estimates
/// @param[in,out] fsk_pulses Pulse_data_t structure for FSK demodulated data
/// @param detector One of FSK_PULSE_DETECTOR_*
void pulse_detect_fsk_finalize(pulse_detect_fsk_t *s, pulse_data_t const *pulses,
        pulse_data_t *fsk_pulses, unsigned detector);

#endif /* INCLUDE_PULSE_DETECT_FSK_H_ */
