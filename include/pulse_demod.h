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
#include "rtl_433_devices.h"


/// Demodulate a plain Pulse Width Modulation signal
/// @return number of events processed
int pulse_demod_pwm_raw(const pulse_data_t *pulses, r_device *device);


#endif /* INCLUDE_PULSE_DEMOD_H_ */
