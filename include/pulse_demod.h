/**
 * Pulse demodulation functions
 * 
 * Binary demodulators (PWM/PPM/Manchester/...) using a pulse data structure as input
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef INCLUDE_PULSE_DEMOD_H_
#define INCLUDE_PULSE_DEMOD_H_

#include <stdint.h>
#include "pulse_detect.h"
#include "rtl_433.h"


/// Demodulate a Pulse Code Modulation signal
///
/// Demodulate a Pulse Code Modulation (PPM) signal with Return-to-Zero encoding.
/// Binary width is fixed and each bit starts with a pulse or not
/// The presence of a pulse is:
/// - Presence of a pulse equals 1
/// - Absence of a pulse equals 0
/// @param device->short_limit: Nominal width of pulse [samples]
/// @param device->long_limit:  Nominal width of bit period [samples]
/// @param device->reset_limit: Maximum gap size before End Of Message [samples].
/// @return number of events processed
int pulse_demod_pcm_rz(const pulse_data_t *pulses, struct protocol_state *device);


/// Demodulate a Pulse Position Modulation signal
///
/// Demodulate a Pulse Position Modulation (PPM) signal consisting of pulses with variable gap.
/// Pulse width may be fixed or variable.
/// Gap between pulses determine the encoding:
/// - Short gap will add a 0 bit
/// - Long  gap will add a 1 bit
/// @param device->short_limit: Threshold between short and long gap [samples]
/// @param device->long_limit:  Maximum gap size before new row of bits [samples]
/// @param device->reset_limit: Maximum gap size before End Of Message [samples].
/// @return number of events processed
int pulse_demod_ppm(const pulse_data_t *pulses, struct protocol_state *device);


/// Demodulate a Pulse Width Modulation signal
///
/// Demodulate a Pulse Width Modulation (PWM) signal consisting of short and long high pulses.
/// Gap between pulses may be of fixed size or variable (e.g. fixed period)
/// - Short pulse will add a 1 bit
/// - Long  pulse will add a 0 bit
/// @param device->short_limit: Threshold between short and long pulse [samples]
/// @param device->long_limit:  Maximum gap size before new row of bits [samples]
/// @param device->reset_limit: Maximum gap size before End Of Message [samples].
/// @param device->demod_arg = 0: Do not remove any startbits
/// @param device->demod_arg = 1: First bit in each message is considered a startbit and not stored in bitbuffer
/// @return number of events processed
int pulse_demod_pwm(const pulse_data_t *pulses, struct protocol_state *device);


/// Demodulate a Pulse Width Modulation signal with three pulse widths
///
/// Demodulate a Pulse Width Modulation (PWM) signal consisting of short, middle and long high pulses.
/// Gap between pulses may be of fixed size or variable (e.g. fixed period)
/// The sync bit will add a new row to the bitbuffer
/// @param device->short_limit: Threshold between short and middle pulse [samples]
/// @param device->long_limit:  Threshold between middle and long pulse [samples]
/// @param device->reset_limit: Maximum gap size before End Of Message [samples].
/// @param device->demod_arg = 0: Short pulse is sync, middle is 0, long is 1
/// @param device->demod_arg = 1: Short pulse is 0, middle is sync, long is 1
/// @param device->demod_arg = 2: Short pulse is 0, middle is 1, long is sync
/// @return number of events processed
int pulse_demod_pwm_ternary(const pulse_data_t *pulses, struct protocol_state *device);


/// Demodulate a Manchester encoded signal with a hardcoded zerobit in front
///
/// Demodulate a Manchester encoded signal where first rising edge is counted as a databit 
/// and therefore always will be zero (Most likely a hardcoded Oregon Scientific peculiarity)
///
/// Clock is recovered from the data based on pulse width. When time since last bit is more 
/// than 1.5 times the clock half period (short_width) it is declared a data edge where:
/// - Rising edge means bit = 0
/// - Falling edge means bit = 1
/// @param device->short_limit: Nominal width of clock half period [samples]
/// @param device->long_limit:  Not used
/// @param device->reset_limit: Maximum gap size before End Of Message [samples].
/// @return number of events processed
int pulse_demod_manchester_zerobit(const pulse_data_t *pulses, struct protocol_state *device);


#endif /* INCLUDE_PULSE_DEMOD_H_ */
