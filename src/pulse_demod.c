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

#include "pulse_demod.h"
#include "bitbuffer.h"
#include <stdio.h>

int pulse_demod_pwm_raw(const pulse_data_t *pulses, struct protocol_state *device) {
	int events = 0;
	bitbuffer_t bits = {0};
	for(unsigned n = 0; n < pulses->num_pulses; ++n) {
		if(pulses->pulse[n] <= (unsigned)device->short_limit) {
			bitbuffer_add_bit(&bits, 1);
		} else {
			bitbuffer_add_bit(&bits, 0);
		}

		if(pulses->gap[n] > (unsigned)device->reset_limit) {
			if (device->callback) {
				events += device->callback(bits.bits_buffer, bits.bits_per_row);
				bitbuffer_clear(&bits);
			} else {
				bitbuffer_print(&bits);
			}
		} else if(pulses->gap[n] >= (unsigned)device->long_limit) {
			bitbuffer_add_row(&bits);
		}
	}
	return events;
}
