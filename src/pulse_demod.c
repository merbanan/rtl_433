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

int pulse_demod_pwm_raw(const pulse_data_t *pulses, r_device *device) {
	int events = 0;
	bitbuffer_t bits = {0};
	for(unsigned n = 0; n < pulses->num_pulses; ++n) {
		if(pulses->pulse[n] <= device->short_limit) {
			bitbuffer_add_bit(&bits, 1);
		} else {
			bitbuffer_add_bit(&bits, 0);
		}

		if(pulses->gap[n] > device->reset_limit) {
            if (device->json_callback) {
                events += device->json_callback(bits.bits_buffer, bits.bits_per_row);
				bitbuffer_clear(&bits);
            } else {
                bitbuffer_print(&bits);
			}
		} else if(pulses->gap[n] >= device->long_limit) {
			bitbuffer_add_row(&bits);
		}
	}
	return events;
}
/*
/// Pulse Width Modulation. No startbit removal
static void pwm_raw_decode(struct dm_state *demod, struct protocol_state* p, int16_t *buf, uint32_t len) {
    unsigned int i;
    for (i = 0; i < len; i++) {
        if (p->start_c) p->sample_counter++;

        // Detect Pulse Start (leading edge)
        if (!p->pulse_start && (buf[i] > demod->level_limit)) {
            p->pulse_start    = 1;
            p->sample_counter = 0;
            // Check for first bit in sequence
            if(!p->start_c) {
                p->start_c = 1;
            }
        }

        // Detect Pulse End (trailing edge)
        if (p->pulse_start && (buf[i] < demod->level_limit)) {
            p->pulse_start      = 0;
            if (p->sample_counter <= p->short_limit) {
                demod_add_bit(p, 1);
            } else {
                demod_add_bit(p, 0);
            }
        }

        // Detect Pulse period overrun
        if (p->sample_counter == p->long_limit) {
                demod_next_bits_packet(p);
        }

        // Detect Pulse exceeding reset limit
        if (p->sample_counter > p->reset_limit) {
            p->sample_counter   = 0;
            p->start_c          = 0;
            p->pulse_start      = 0;

            if (p->callback)
                events+=p->callback(p->bits_buffer, p->bits_per_row);
            else
                demod_print_bits_packet(p);

            demod_reset_bits_packet(p);
        }
    }
}

*/
