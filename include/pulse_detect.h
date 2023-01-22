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

/// FSK pulse detector to use.
enum {
    FSK_PULSE_DETECT_OLD,
    FSK_PULSE_DETECT_NEW,
    FSK_PULSE_DETECT_AUTO,
    FSK_PULSE_DETECT_END,
};

typedef struct pulse_detect pulse_detect_t;

pulse_detect_t *pulse_detect_create(void);

void pulse_detect_free(pulse_detect_t *pulse_detect);

/// Set pulse detector level values.
///
/// @param pulse_detect The pulse_detect instance
/// @param use_mag_est Use magnitude instead of amplitude
/// @param fixed_high_level Manual high level override, default is 0 (auto)
/// @param min_high_level Minimum high level, default is -12 dB
/// @param high_low_ratio Minimum signal noise ratio, default is 9 dB
/// @param verbosity Debug output verbosity, 0=None, 1=Levels, 2=Histograms
void pulse_detect_set_levels(pulse_detect_t *pulse_detect, int use_mag_est, float fixed_high_level, float min_high_level, float high_low_ratio, int verbosity);

/// Demodulate On/Off Keying (OOK) and Frequency Shift Keying (FSK) from an envelope signal.
///
/// Function is stateful and can be called with chunks of input data.
///
/// @param pulse_detect The pulse_detect instance
/// @param envelope_data Samples with amplitude envelope of carrier
/// @param fm_data Samples with frequency offset from center frequency
/// @param len Number of samples in input buffers
/// @param samp_rate Sample rate in samples per second
/// @param sample_offset Offset tracking for ringbuffer
/// @param[in,out] pulses Will return a pulse_data_t structure
/// @param[in,out] fsk_pulses Will return a pulse_data_t structure for FSK demodulated data
/// @param fpdm Index of filter setting to use
/// @return if a package is detected
/// @retval 0 all input sample data is processed
/// @retval 1 OOK package is detected (but all sample data is still not completely processed)
/// @retval 2 FSK package is detected (but all sample data is still not completely processed)
int pulse_detect_package(pulse_detect_t *pulse_detect, int16_t const *envelope_data, int16_t const *fm_data, int len, uint32_t samp_rate, uint64_t sample_offset, pulse_data_t *pulses, pulse_data_t *fsk_pulses, unsigned fpdm);

#endif /* INCLUDE_PULSE_DETECT_H_ */
