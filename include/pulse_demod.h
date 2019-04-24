/** @file
    Pulse demodulation functions.

    Binary demodulators (PWM/PPM/Manchester/...) using a pulse data structure as input

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_PULSE_DEMOD_H_
#define INCLUDE_PULSE_DEMOD_H_

#include "pulse_detect.h"
#include "r_device.h"

/// Demodulate a Pulse Code Modulation signal.
///
/// Demodulate a Pulse Code Modulation (PCM) signal where bit width
/// is fixed and each bit starts with a pulse or not. It may be either
/// Return-to-Zero (RZ) encoding, where pulses are shorter than bit width
/// or Non-Return-to-Zero (NRZ) encoding, where pulses are equal to bit width
/// The presence of a pulse is:
/// - Presence of a pulse equals 1
/// - Absence of a pulse equals 0
/// @param device->short_width: Nominal width of pulse [us]
/// @param device->long_width:  Nominal width of bit period [us]
/// @param device->reset_limit: Maximum gap size before End Of Message [us].
/// @return number of events processed
int pulse_demod_pcm(const pulse_data_t *pulses, r_device *device);

/// Demodulate a Pulse Position Modulation signal.
///
/// Demodulate a Pulse Position Modulation (PPM) signal consisting of pulses with variable gap.
/// Pulse width may be fixed or variable.
/// Gap between pulses determine the encoding:
/// - Short gap will add a 0 bit
/// - Long  gap will add a 1 bit
/// @param device->short_width: Nominal width of '0' [us]
/// @param device->long_width:  Nominal width of '1' [us]
/// @param device->reset_limit: Maximum gap size before End Of Message [us].
/// @param device->gap_limit:   Maximum gap size before new row of bits [us]
/// @param device->tolerance:   Maximum deviation from nominal widths (optional, raw if 0) [us]
/// @return number of events processed
int pulse_demod_ppm(const pulse_data_t *pulses, r_device *device);

/// Demodulate a Pulse Width Modulation signal.
///
/// Demodulate a Pulse Width Modulation (PWM) signal consisting of short, long, and optional sync pulses.
/// Gap between pulses may be of fixed size or variable (e.g. fixed period)
/// - Short pulse will add a 1 bit
/// - Long pulse will add a 0 bit
/// - Sync pulse (optional) will add a new row to bitbuffer
/// @param device->short_width: Nominal width of '1' [us]
/// @param device->long_width:  Nominal width of '0' [us]
/// @param device->reset_limit: Maximum gap size before End Of Message [us].
/// @param device->gap_limit:   Maximum gap size before new row of bits [us]
/// @param device->sync_width:  Nominal width of sync pulse (optional) [us]
/// @param device->tolerance:   Maximum deviation from nominal widths (optional, raw if 0) [us]
/// @return number of events processed
int pulse_demod_pwm(const pulse_data_t *pulses, r_device *device);

/// Demodulate a Manchester encoded signal with a hardcoded zerobit in front.
///
/// Demodulate a Manchester encoded signal where first rising edge is counted as a databit
/// and therefore always will be zero (Most likely a hardcoded Oregon Scientific peculiarity)
///
/// Clock is recovered from the data based on pulse width. When time since last bit is more
/// than 1.5 times the clock half period (short_width) it is declared a data edge where:
/// - Rising edge means bit = 0
/// - Falling edge means bit = 1
/// @param device->short_width: Nominal width of clock half period [us]
/// @param device->long_width:  Not used
/// @param device->reset_limit: Maximum gap size before End Of Message [us].
/// @return number of events processed
int pulse_demod_manchester_zerobit(const pulse_data_t *pulses, r_device *device);

/// Demodulate a Differential Manchester Coded signal.
///
/// No level shift within the clock cycle translates to a logic 0
/// One level shift within the clock cycle translates to a logic 1
/// Each clock cycle begins with a level shift
///
/// +---+   +---+   +-------+       +  high
/// |   |   |   |   |       |       |
/// |   |   |   |   |       |       |
/// +   +---+   +---+       +-------+  low
///
/// ^       ^       ^       ^       ^  clock cycle
/// |   1   |   1   |   0   |   0   |  translates as
///
/// @param device->short_width: Width in samples of '1' [us]
/// @param device->long_width:  Width in samples of '0' [us]
/// @param device->reset_limit: Maximum gap size before End Of Message [us].
/// @param device->tolerance:   Maximum deviation from nominal widths [us]
/// @return number of events processed
int pulse_demod_dmc(const pulse_data_t *pulses, r_device *device);

/// Demodulate a raw Pulse Interval and Width Modulation signal.
///
/// Each level shift is a new bit.
/// A short interval is a logic 1, a long interval a logic 0
///
/// @param device->short_width: Nominal width of a bit [us]
/// @param device->long_width:  Maximum width of a run of bits [us]
/// @param device->reset_limit: Maximum gap size before End Of Message [us].
/// @param device->tolerance:   Maximum deviation from nominal widths [us]
/// @return number of events processed
int pulse_demod_piwm_raw(const pulse_data_t *pulses, r_device *device);

/// Demodulate a differential Pulse Interval and Width Modulation signal.
///
/// Each level shift is a new bit.
/// A short interval is a logic 1, a long interval a logic 0
///
/// @param device->short_width: Nominal width of '1' [us]
/// @param device->long_width:  Nominal width of '0' [us]
/// @param device->reset_limit: Maximum gap size before End Of Message [us].
/// @param device->tolerance:   Maximum deviation from nominal widths [us]
/// @return number of events processed
int pulse_demod_piwm_dc(const pulse_data_t *pulses, r_device *device);

int pulse_demod_osv1(const pulse_data_t *pulses, r_device *device);

/// Simulate demodulation using a given signal code string.
///
/// The (optionally "0x" prefixed) hex code is processed into a bitbuffer_t.
/// Each row is optionally prefixed with a length enclosed in braces "{}" or
/// separated with a slash "/" character. Whitespace is ignored.
/// Device params are disregarded.
/// @return number of events processed
int pulse_demod_string(const char *code, r_device *device);

#endif /* INCLUDE_PULSE_DEMOD_H_ */
