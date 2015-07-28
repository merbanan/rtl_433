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
#include "util.h"
#include <stdio.h>
#include <stdlib.h>


int pulse_demod_pcm_rz(const pulse_data_t *pulses, struct protocol_state *device)
{
	int events = 0;
	bitbuffer_t bits = {0};
	const int MAX_ZEROS = device->reset_limit / device->long_limit;
	const int TOLERANCE = device->long_limit / 10;		// Tolerance is 10% of a bit period
	
	for(unsigned n = 0; n < pulses->num_pulses; ++n) {
		int periods = (pulses->pulse[n] + pulses->gap[n] + device->long_limit/2) / device->long_limit;	// Number of bits

		// Validate data
		if ((abs(pulses->pulse[n] - device->short_limit) < TOLERANCE)					// Pulse must be within tolerance
		 && ((abs(pulses->pulse[n] + pulses->gap[n] - periods * device->long_limit) < TOLERANCE)	// Pulse + Gap must be within tolerance
		  || (pulses->gap[n] > (unsigned)device->reset_limit))							// .. or we are above our limit
		) {
			// The pulse is a one
			bitbuffer_add_bit(&bits, 1);

			// Add run of zeros
			periods--;							// First period is the one
			periods = min(periods, MAX_ZEROS); 	// Dont overflow at end of message
			for (int n=0; n < periods; ++n) {
				bitbuffer_add_bit(&bits, 0);
			}
		} else {
			// Data is corrupt
			if (debug_output > 3) {
			        fprintf(stderr,"bitbuffer cleared at %d: pulse %d, gap %d, period %d\n",
					n,pulses->pulse[n],pulses->gap[n],
					pulses->pulse[n] + pulses->gap[n]);
			}
			bitbuffer_clear(&bits);
		}

		// End of Message?
		if ((pulses->gap[n] > (unsigned)device->reset_limit)
		 && (bits.bits_per_row[0] > 0)		// Only if data has been accumulated
		) {
			if (device->callback) {
				events += device->callback(&bits);
			}
			// Debug printout
			if(!device->callback || (debug_output && events > 0)) {
				fprintf(stderr, "\npulse_demod_pcm(): %s \n", device->name);
				bitbuffer_print(&bits);
			}
			bitbuffer_clear(&bits);
		}
	} // for
	return events;
}


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
				events += device->callback(&bits);
			}
			// Debug printout
			if(!device->callback || (debug_output && events > 0)) {
				fprintf(stderr, "\npulse_demod_ppm(): %s \n", device->name);
				bitbuffer_print(&bits);
			}
			bitbuffer_clear(&bits);
		}
	} // for pulses
	return events;
}


int pulse_demod_pwm(const pulse_data_t *pulses, struct protocol_state *device) {
	int events = 0;
	int start_bit_detected = 0;
	bitbuffer_t bits = {0};
	int start_bit = device->demod_arg;

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
				events += device->callback(&bits);
			}
			// Debug printout
			if(!device->callback || (debug_output && events > 0)) {
				fprintf(stderr, "\npulse_demod_pwm(): %s \n", device->name);
				bitbuffer_print(&bits);
			}
			bitbuffer_clear(&bits);
			start_bit_detected = 0;
		// Check for new packet in multipacket
		} else if(pulses->gap[n] > (unsigned)device->long_limit) {
			bitbuffer_add_row(&bits);
			start_bit_detected = 0;
		}
	}
	return events;
}


int pulse_demod_pwm_ternary(const pulse_data_t *pulses, struct protocol_state *device)
{
	int events = 0;
	bitbuffer_t bits = {0};
	unsigned sync_bit = device->demod_arg;
	
	for(unsigned n = 0; n < pulses->num_pulses; ++n) {
		// Short pulse
		if (pulses->pulse[n] < (unsigned)device->short_limit) {
			if (sync_bit == 0) {
				bitbuffer_add_row(&bits);
			} else {
				bitbuffer_add_bit(&bits, 0);
			}
		// Middle pulse
		} else if (pulses->pulse[n] < (unsigned)device->long_limit) {
			if (sync_bit == 0) {
				bitbuffer_add_bit(&bits, 0);
			} else if (sync_bit == 1) {
				bitbuffer_add_row(&bits);
			} else {
				bitbuffer_add_bit(&bits, 1);
			}
		// Long pulse
		} else {
			if (sync_bit == 2) {
				bitbuffer_add_row(&bits);
			} else {
				bitbuffer_add_bit(&bits, 1);
			}
		} 

		// End of Message?
		if(pulses->gap[n] > (unsigned)device->reset_limit) {
			if (device->callback) {
				events += device->callback(&bits);
			}
			// Debug printout
			if(!device->callback || (debug_output && events > 0)) {
				fprintf(stderr, "\npulse_demod_pwm_ternary(): %s \n", device->name);
				bitbuffer_print(&bits);
			}
			bitbuffer_clear(&bits);
		}
	} // for
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
			int newevents = 0;
			if (device->callback) {
				events += device->callback(&bits);
			}
			// Debug printout
			if(!device->callback || (debug_output && events > 0)) {
				fprintf(stderr, "\npulse_demod_manchester_zerobit(): %s \n", device->name);
				bitbuffer_print(&bits);
			}
			bitbuffer_clear(&bits);
			bitbuffer_add_bit(&bits, 0);		// Prepare for new message with hardcoded 0
			time_since_last = 0;
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
