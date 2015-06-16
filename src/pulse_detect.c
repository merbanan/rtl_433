/**
 * Pulse detection functions
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "pulse_detect.h"
#include <stdio.h>


/**
 * Clear the content of a pulse_data_t structure
 */
void pulse_data_clear(pulse_data_t *data) {
	data->num_pulses = 0;
	for(unsigned n = 0; n < MAX_PULSES; ++n) {
		data->pulse[n] = 0;
		data->gap[n] = 0;
	}
}


/**
 * Print the content of a pulse_data_t structure (for debug)
 */
void pulse_data_print(const pulse_data_t *data) {
    fprintf(stderr, "Pulse data: %u pulses\n", data->num_pulses);
	for(unsigned n = 0; n < data->num_pulses; ++n) {
		fprintf(stderr, "[%3u] Pulse: %4u, Gap: %4u\n", n, data->pulse[n], data->gap[n]);
	}
}


/**
 * Internal state data for detect_pulse_package()
 */
typedef struct {
	enum {
		PULSE_STATE_IDLE  = 0,
		PULSE_STATE_PULSE = 1,
		PULSE_STATE_GAP	  = 2
	} state;
	unsigned int pulse_length;		// Counter for internal pulse detection
	unsigned int max_pulse;			// Size of biggest pulse detected
	unsigned int max_gap;			// Size of biggest gap detected

	unsigned int data_counter;		// Counter for how much of data chunck is processed
} pulse_state_t;
static pulse_state_t pulse_state;


/**
 * Demodulate On/Off Keying from an envelope signal
 * Function is stateful and can be called with chunks of input data
 * @return 0 if all input data is processed
 * @return 1 if package is detected (but data is still not completely processed)
 */
int detect_pulse_package(const int16_t *envelope_data, uint32_t len, int16_t level_limit, pulse_data_t *pulses) {

	pulse_state_t *s = &pulse_state;

	// Process all new samples
	while(s->data_counter < len) {
		switch (s->state) {
			case PULSE_STATE_IDLE:
				s->pulse_length = 0;
				s->max_pulse = 0;
				s->max_gap = 0;
				if (envelope_data[s->data_counter] > level_limit) {
					s->state = PULSE_STATE_PULSE;
				}
				break;
			case PULSE_STATE_PULSE:
				s->pulse_length++;
				// End of pulse detected?
				if (envelope_data[s->data_counter] < level_limit) {		// Gap?
					pulses->pulse[pulses->num_pulses] = s->pulse_length;	// Store pulse width

					// EOP if pulse is too long
					if (s->pulse_length > PULSE_DETECT_MAX_PULSE_LENGTH) {
						pulses->num_pulses++;	// Store last pulse (with no gap)
						s->state = PULSE_STATE_IDLE;
						return 1;	// End Of Package!!
					}

					// Find largest pulse
					if(s->pulse_length > s->max_pulse) {
						s->max_pulse = s->pulse_length;
					}
					s->pulse_length = 0;
					s->state = PULSE_STATE_GAP;
				}
				break;
			case PULSE_STATE_GAP:
				s->pulse_length++;
				// New pulse detected?
				if (envelope_data[s->data_counter] > level_limit) {		// New pulse?
					pulses->gap[pulses->num_pulses] = s->pulse_length;	// Store gap width
					pulses->num_pulses++;	// Next pulse

					// EOP if too many pulses
					if (pulses->num_pulses >= MAX_PULSES) {
						s->state = PULSE_STATE_IDLE;
						return 1;	// End Of Package!!
					}

					// Find largest gap
					if(s->pulse_length > s->max_gap) {
						s->max_gap = s->pulse_length;
					}
					s->pulse_length = 0;
					s->state = PULSE_STATE_PULSE;
				}

				// EOP if gap is too long
				if ((s->pulse_length > (s->max_pulse * PULSE_DETECT_MAX_GAP_RATIO))
//				 || (s->pulse_length > (s->max_gap * PULSE_DETECT_MAX_GAP_RATIO) && s->max_gap !=0)
				 ) {
					pulses->gap[pulses->num_pulses] = s->pulse_length;	// Store gap width
					pulses->num_pulses++;	// Store last pulse
					s->state = PULSE_STATE_IDLE;
					return 1;	// End Of Package!!
				}
				break;
			default:
				fprintf(stderr, "demod_OOK(): Unknown state!!\n");
				s->state = PULSE_STATE_IDLE;
		} // switch
		// Todo: check for too many pulses
		s->data_counter++;
	} // while

	s->data_counter = 0;
	return 0;	// Out of data
}


