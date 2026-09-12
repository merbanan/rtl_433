/** @file
    Pulse detection functions.

    Copyright (C) 2015 Tommy Vestermark
    Copyright (C) 2020 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_PULSE_DETECT_H_
#define INCLUDE_PULSE_DETECT_H_

#include <stdint.h>
#include <stdio.h>
#include "pulse_data.h"
#include "data.h"

/// Package types.
enum package_types {
    PULSE_DATA_OOK = 1,
    PULSE_DATA_FSK = 2,
};

/// Envelope detector events, independent of the modulation inside a pulse.
enum pulse_detect_events {
    PULSE_DETECT_END,   ///< Input buffer consumed; the package may be incomplete.
    PULSE_DETECT_OOK,   ///< Complete envelope pulse train.
    PULSE_DETECT_START, ///< New envelope package; reset downstream detectors.
    PULSE_DETECT_PULSE, ///< Confirmed gap, or end-of-input while inside a pulse.
};

/// Newly consumed samples of the first carrier pulse, including short gaps.
typedef struct {
    unsigned start; ///< Index in the current input buffer.
    unsigned len;   ///< Sample count; never extends beyond the current buffer.
} pulse_detect_span_t;

typedef struct pulse_detect pulse_detect_t;

pulse_detect_t *pulse_detect_create(void);

void pulse_detect_free(pulse_detect_t *pulse_detect);

/// Reset pulse detector to initial values.
void pulse_detect_reset(pulse_detect_t *pulse_detect);

/// Accept a carrier package at PULSE_DETECT_PULSE and discard its OOK envelope.
/// Preserves level estimates and resumes at the gap boundary in the same buffer.
void pulse_detect_skip_package(pulse_detect_t *pulse_detect);

/// Set pulse detector level values.
///
/// @param pulse_detect The pulse_detect instance
/// @param use_mag_est Use magnitude instead of amplitude
/// @param fixed_high_level Manual high level override, default is 0 (auto)
/// @param min_high_level Minimum high level, default is -12 dB
/// @param high_low_ratio Minimum signal noise ratio, default is 9 dB
/// @param verbosity Debug output verbosity, 0=None, 1=Levels, 2=Histograms
void pulse_detect_set_levels(pulse_detect_t *pulse_detect, int use_mag_est, float fixed_high_level, float min_high_level, float high_low_ratio, int verbosity);

/// Detect an OOK envelope and expose carrier spans for downstream demodulation.
///
/// Call repeatedly with the same buffer until PULSE_DETECT_END is returned.
/// Process span even on PULSE_DETECT_END, retaining downstream state between
/// buffers. Pass len=0 to flush at end-of-input. At PULSE_DETECT_PULSE a caller
/// may accept another modulation with pulse_detect_skip_package(), otherwise
/// the next call continues building the OOK package.
///
/// @param pulse_detect The pulse_detect instance
/// @param envelope_data Samples with amplitude envelope of carrier
/// @param fm_data Frequency samples used only for the OOK carrier estimate
/// @param len Number of samples in input buffers
/// @param samp_rate Sample rate in samples per second
/// @param sample_offset Offset tracking for ringbuffer
/// @param[in,out] pulses Will return a pulse_data_t structure
/// @param[out] span Newly consumed samples of the first carrier pulse
/// @return An enum pulse_detect_events value
int pulse_detect_package(pulse_detect_t *pulse_detect, int16_t const *envelope_data, int16_t const *fm_data, int len, uint32_t samp_rate, uint64_t sample_offset, pulse_data_t *pulses, pulse_detect_span_t *span);

#endif /* INCLUDE_PULSE_DETECT_H_ */
