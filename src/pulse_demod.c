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


int pulse_demod_ppm(const pulse_data_t *pulses, struct protocol_state *device) {
	int events = 0;
	bitbuffer_t bits = {0};
	
	for(unsigned n = 0; n < pulses->num_pulses; ++n) {
		// Short gap
		if(pulses->gap[n] < (unsigned)device->short_limit) {
			bitbuffer_add_bit(&bits, 0);
		// Long gap
		} else if(pulses->gap[n] < (unsigned)device->long_limit) {
			bitbuffer_add_bit(&bits, 1);
		// Check for new packet in multipacket
		} else if(pulses->gap[n] < (unsigned)device->reset_limit) {
			bitbuffer_add_row(&bits);
		// End of Message?
		} else {
			if (device->callback) {
				events += device->callback(bits.bits_buffer, bits.bits_per_row);
				bitbuffer_clear(&bits);
			} else {
				bitbuffer_print(&bits);
			}
		}
	} // for pulses
	return events;
}


int pulse_demod_pwm(const pulse_data_t *pulses, struct protocol_state *device, int start_bit) {
	int events = 0;
	int start_bit_detected = 0;
	bitbuffer_t bits = {0};
	
	for(unsigned n = 0; n < pulses->num_pulses; ++n) {
		
		// Should we disregard startbit?
		if(start_bit == 1 && start_bit_detected == 0) {	
			start_bit_detected = 1;
		} else {
			// Detect pulse width
			if(pulses->pulse[n] <= (unsigned)device->short_limit) {
				bitbuffer_add_bit(&bits, 1);
			} else {
				bitbuffer_add_bit(&bits, 0);
			}
		}

		// End of Message?
		if(pulses->gap[n] > (unsigned)device->reset_limit) {
			if (device->callback) {
				events += device->callback(bits.bits_buffer, bits.bits_per_row);
				bitbuffer_clear(&bits);
				start_bit_detected = 0;
			} else {
				bitbuffer_print(&bits);
			}
		// Check for new packet in multipacket
		} else if(pulses->gap[n] > (unsigned)device->long_limit) {
			bitbuffer_add_row(&bits);
			start_bit_detected = 0;
		}
	}
	return events;
}


int pulse_demod_manchester_zerobit(const pulse_data_t *pulses, struct protocol_state *device) {
	int events = 0;
	unsigned time_since_last = 0;
	bitbuffer_t bits = {0};
	
	// First rising edge is allways counted as a zero (Seems to be hardcoded policy for the Oregon Scientific sensors...)
	bitbuffer_add_bit(&bits, 0);

	for(unsigned n = 0; n < pulses->num_pulses; ++n) {
		// Falling edge is on end of pulse
		if(pulses->pulse[n] + time_since_last > (unsigned)(device->short_limit + (device->short_limit>>1))) {
			// Last bit was recorded more than short_limit*1.5 samples ago 
			// so this pulse start must be a data edge (falling data edge means bit = 1) 
			bitbuffer_add_bit(&bits, 1);
			time_since_last = 0;
		} else {
			time_since_last += pulses->pulse[n];
		}
		
		// End of Message?
		if(pulses->gap[n] > (unsigned)device->reset_limit) {		
			if (device->callback) {
				events += device->callback(bits.bits_buffer, bits.bits_per_row);
				bitbuffer_clear(&bits);
				bitbuffer_add_bit(&bits, 0);		// Prepare for new message with hardcoded 0
				time_since_last = 0;
			} else {
				bitbuffer_print(&bits);
			}
		// Rising edge is on end of gap
		} else if(pulses->gap[n] + time_since_last > (unsigned)(device->short_limit + (device->short_limit>>1))) {
			// Last bit was recorded more than short_limit*1.5 samples ago 
			// so this pulse end is a data edge (rising data edge means bit = 0) 
			bitbuffer_add_bit(&bits, 0);
			time_since_last = 0;
		} else {
			time_since_last += pulses->gap[n];
		}
	}
	return events;
}
