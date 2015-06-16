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

#define MAX_HIST_BINS 16

typedef struct {
	unsigned count;
	unsigned sum;
	unsigned mean;
	unsigned min;
	unsigned max;
} hist_bin_t;

typedef struct {
	unsigned bins_count;
	hist_bin_t bins[MAX_HIST_BINS];
} histogram_t;

/**
 * Generate a histogram (unsorted)
 */
void histogram_sum(const unsigned *data, unsigned len, float tolerance, histogram_t *hist) {
	unsigned bin;	// Iterator will be used outside for! 
	float t_upper = 1.0 + tolerance;
	float t_lower = 1.0 - tolerance;

	for(unsigned n = 0; n < len; ++n) {
		for(bin = 0; bin < hist->bins_count; ++bin) {
			if((data[n] > (t_lower * hist->bins[bin].mean)) 
			&& (data[n] < (t_upper * hist->bins[bin].mean))
			) {
				hist->bins[bin].count++;
				hist->bins[bin].sum += data[n];
				hist->bins[bin].mean = hist->bins[bin].sum / hist->bins[bin].count;
				hist->bins[bin].min = (data[n] < hist->bins[bin].min ? data[n] : hist->bins[bin].min);
				hist->bins[bin].max = (data[n] > hist->bins[bin].max ? data[n] : hist->bins[bin].max);
				break;	// Match found!
			}
		}
		// No match found?
		if(bin == hist->bins_count && bin < MAX_HIST_BINS) {
			hist->bins[bin].count	= 1;
			hist->bins[bin].sum		= data[n];
			hist->bins[bin].mean	= data[n];
			hist->bins[bin].min		= data[n];
			hist->bins[bin].max		= data[n];
			hist->bins_count++;
		} // for bin
	} // for data
}


/**
 * Print a histogram
 */
void histogram_print(const histogram_t *hist) {
	for(unsigned n = 0; n < hist->bins_count; ++n) {
		fprintf(stderr, " [%2u] mean: %4u (%u/%u),\t count: %3u\n", n, 
			hist->bins[n].mean, 
			hist->bins[n].min, 
			hist->bins[n].max, 
			hist->bins[n].count);
	}
}


#define TOLERANCE (0.2)		// 20% tolerance should still discern between the pulse widths: 0.33, 0.66, 1.0

/**
 * Analyze and print result
 */
void pulse_analyzer(const pulse_data_t *data)
{
	// Generate pulse period data
	pulse_data_t pulse_periods = {0};
	pulse_periods.num_pulses = data->num_pulses;
	for(unsigned n = 0; n < pulse_periods.num_pulses; ++n) {
		pulse_periods.pulse[n] = data->pulse[n] + data->gap[n];
	}

	histogram_t hist_pulses = {0};
	histogram_t hist_gaps = {0};
	histogram_t hist_periods = {0};

	histogram_sum(data->pulse, data->num_pulses, 0.2, &hist_pulses);
	histogram_sum(data->gap, data->num_pulses-1, 0.2, &hist_gaps);						// Leave out last gap (end)
	histogram_sum(pulse_periods.pulse, pulse_periods.num_pulses-1, 0.1, &hist_periods);	// Leave out last gap (end)

	fprintf(stderr, "\nAnalyzing pulses...\n");
	fprintf(stderr, "Total number of pulses: %u\n", data->num_pulses);
	fprintf(stderr, "Pulse width distribution:\n");
	histogram_print(&hist_pulses);
	fprintf(stderr, "Gap width distribution:\n");
	histogram_print(&hist_gaps);
	fprintf(stderr, "Pulse period distribution:\n");
	histogram_print(&hist_periods);
	
	fprintf(stderr, "Guessing modulation: ");
	if(data->num_pulses == 1) {
		fprintf(stderr, "Single pulse detected. Probably Frequency Shift Keying or just noise...\n");
	} else if(hist_pulses.bins_count == 1 && hist_gaps.bins_count == 2 && hist_periods.bins_count == 2) {
		fprintf(stderr, "Pulse Position Modulation with fixed pulse width\n");
	} else if(hist_pulses.bins_count == 2 && hist_gaps.bins_count == 2 && hist_periods.bins_count == 1) {
		fprintf(stderr, "Pulse Width Modulation with fixed period\n");
	} else if(hist_pulses.bins_count == 2 && hist_gaps.bins_count == 1 && hist_periods.bins_count == 2) {
		fprintf(stderr, "Pulse Width Modulation with fixed gap\n");
	} else if(hist_pulses.bins_count == 2 && hist_gaps.bins_count == 2 && hist_periods.bins_count == 3) {
		fprintf(stderr, "Manchester coding\n");
	} else if(hist_pulses.bins_count == 3 && hist_gaps.bins_count == 3 && hist_periods.bins_count == 1) {
		fprintf(stderr, "Pulse Width Modulation with startbit/delimiter\n");
	} else {
		fprintf(stderr, "No clue...\n");
	}
	
	
	fprintf(stderr, "\n");
}

