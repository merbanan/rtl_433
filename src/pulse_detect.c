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
#include "util.h"
#include <stdio.h>
#include <stdlib.h>

void pulse_data_clear(pulse_data_t *data) {
	data->num_pulses = 0;
	for(unsigned n = 0; n < PD_MAX_PULSES; ++n) {
		data->pulse[n] = 0;
		data->gap[n] = 0;
	}
}


void pulse_data_print(const pulse_data_t *data) {
    fprintf(stderr, "Pulse data: %u pulses\n", data->num_pulses);
	for(unsigned n = 0; n < data->num_pulses; ++n) {
		fprintf(stderr, "[%3u] Pulse: %4u, Gap: %4u, Period: %4u\n", n, data->pulse[n], data->gap[n], data->pulse[n] + data->gap[n]);
	}
}


/// Internal state data for detect_pulse_package()
typedef struct {
	enum {
		PD_STATE_IDLE  = 0,
		PD_STATE_PULSE = 1,
		PD_STATE_GAP	  = 2
	} state;
	unsigned int pulse_length;		// Counter for internal pulse detection
	unsigned int max_pulse;			// Size of biggest pulse detected
	unsigned int max_gap;			// Size of biggest gap detected

	unsigned int data_counter;		// Counter for how much of data chunck is processed
} pulse_state_t;
static pulse_state_t pulse_state;


int detect_pulse_package(const int16_t *envelope_data, uint32_t len, int16_t level_limit, uint32_t samp_rate, pulse_data_t *pulses) {
	const unsigned int samples_per_ms = samp_rate / 1000;
	pulse_state_t *s = &pulse_state;

	// Process all new samples
	while(s->data_counter < len) {
		switch (s->state) {
			case PD_STATE_IDLE:
				s->pulse_length = 0;
				s->max_pulse = 0;
				s->max_gap = 0;
				if (envelope_data[s->data_counter] > level_limit) {
					s->state = PD_STATE_PULSE;
				}
				break;
			case PD_STATE_PULSE:
				s->pulse_length++;
				// End of pulse detected?
				if (envelope_data[s->data_counter] < level_limit) {		// Gap?
					pulses->pulse[pulses->num_pulses] = s->pulse_length;	// Store pulse width

					// EOP if pulse is too long
					if (s->pulse_length > (PD_MAX_PULSE_MS * samples_per_ms)) {
						pulses->num_pulses++;	// Store last pulse (with no gap)
						s->state = PD_STATE_IDLE;
						return 1;	// End Of Package!!
					}

					// Find largest pulse
					if(s->pulse_length > s->max_pulse) {
						s->max_pulse = s->pulse_length;
					}
					s->pulse_length = 0;
					s->state = PD_STATE_GAP;
				}
				break;
			case PD_STATE_GAP:
				s->pulse_length++;
				// New pulse detected?
				if (envelope_data[s->data_counter] > level_limit) {		// New pulse?
					pulses->gap[pulses->num_pulses] = s->pulse_length;	// Store gap width
					pulses->num_pulses++;	// Next pulse

					// EOP if too many pulses
					if (pulses->num_pulses >= PD_MAX_PULSES) {
						s->state = PD_STATE_IDLE;
						return 1;	// End Of Package!!
					}

					// Find largest gap
					if(s->pulse_length > s->max_gap) {
						s->max_gap = s->pulse_length;
					}
					s->pulse_length = 0;
					s->state = PD_STATE_PULSE;
				}

				// EOP if gap is too long
				if (((s->pulse_length > (PD_MAX_GAP_RATIO * s->max_pulse))	// gap/pulse ratio exceeded
				 && (s->pulse_length > (PD_MIN_GAP_MS * samples_per_ms)))	// Minimum gap exceeded
				 || (s->pulse_length > (PD_MAX_GAP_MS * samples_per_ms))	// maximum gap exceeded
				 ) {
					pulses->gap[pulses->num_pulses] = s->pulse_length;	// Store gap width
					pulses->num_pulses++;	// Store last pulse
					s->state = PD_STATE_IDLE;
					return 1;	// End Of Package!!
				}
				break;
			default:
				fprintf(stderr, "demod_OOK(): Unknown state!!\n");
				s->state = PD_STATE_IDLE;
		} // switch
		// Todo: check for too many pulses
		s->data_counter++;
	} // while

	s->data_counter = 0;
	return 0;	// Out of data
}

#define MAX_HIST_BINS 16

/// Histogram data for single bin
typedef struct {
	unsigned count;
	unsigned sum;
	unsigned mean;
	unsigned min;
	unsigned max;
} hist_bin_t;

/// Histogram data for all bins
typedef struct {
	unsigned bins_count;
	hist_bin_t bins[MAX_HIST_BINS];
} histogram_t;


/// Generate a histogram (unsorted)
void histogram_sum(histogram_t *hist, const unsigned *data, unsigned len, float tolerance) {
	unsigned bin;	// Iterator will be used outside for!

	for(unsigned n = 0; n < len; ++n) {
		for(bin = 0; bin < hist->bins_count; ++bin) {
			int bn = data[n];
			int bm = hist->bins[bin].mean;
			if (abs(bn - bm) < (tolerance * max(bn, bm))) {
				hist->bins[bin].count++;
				hist->bins[bin].sum += data[n];
				hist->bins[bin].mean = hist->bins[bin].sum / hist->bins[bin].count;
				hist->bins[bin].min	= min(data[n], hist->bins[bin].min);
				hist->bins[bin].max	= max(data[n], hist->bins[bin].max);
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


/// Fuse histogram bins with means within tolerance
void histogram_fuse_bins(histogram_t *hist, float tolerance) {
	hist_bin_t	zerobin = {0};
	if (hist->bins_count < 2) return;		// Avoid underflow
	// Compare all bins
	for(unsigned n = 0; n < hist->bins_count-1; ++n) {
		for(unsigned m = n+1; m < hist->bins_count; ++m) {
			int bn = hist->bins[n].mean;
			int bm = hist->bins[m].mean;
			if (abs(bn - bm) < (tolerance * max(bn, bm))) {
				// Fuse data for bin[n] and bin[m]
				hist->bins[n].count += hist->bins[m].count;
				hist->bins[n].sum	+= hist->bins[m].sum;
				hist->bins[n].mean 	= hist->bins[n].sum / hist->bins[n].count;
				hist->bins[n].min 	= min(hist->bins[n].min, hist->bins[m].min);
				hist->bins[n].max 	= max(hist->bins[n].max, hist->bins[m].max);
				// Delete bin[m]
				for(unsigned l = m; l < hist->bins_count-1; ++l) {
					hist->bins[l] = hist->bins[l+1];
				}
				hist->bins_count--;
				hist->bins[hist->bins_count] = zerobin;
				m--;	// Compare new bin in same place!
			} // if within tolerance
		} // for m
	} // for n
}


/// Print a histogram
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

/// Analyze the statistics of a pulse data structure and print result
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

	// Generate statistics
	histogram_sum(&hist_pulses, data->pulse, data->num_pulses, TOLERANCE);
	histogram_sum(&hist_gaps, data->gap, data->num_pulses-1, TOLERANCE);						// Leave out last gap (end)
	histogram_sum(&hist_periods, pulse_periods.pulse, pulse_periods.num_pulses-1, TOLERANCE);	// Leave out last gap (end)

	// Fuse overlapping bins
	histogram_fuse_bins(&hist_pulses, TOLERANCE);
	histogram_fuse_bins(&hist_gaps, TOLERANCE);
	histogram_fuse_bins(&hist_periods, TOLERANCE);

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
	} else if(hist_pulses.bins_count == 1 && hist_gaps.bins_count > 1 && hist_periods.bins_count > 1) {
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

