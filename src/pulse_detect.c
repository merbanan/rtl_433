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
	*data = (const pulse_data_t) {
		 0,
	};
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

	unsigned int data_counter;		// Counter for how much of data chunck is processed

	unsigned int fsk_pulse_length;		// Counter for internal FSK pulse detection
	enum {
		PD_STATE_FSK_HIGH	= 0,	// High pulse
		PD_STATE_FSK_LOW	= 1		// Low pulse (gap)
	} state_fsk;

	int16_t fm_base_est;			// Estimate for the FM base frequency for FSK
	int16_t fm_delta_est;			// Estimate for the FM frequency delta for FSK

} pulse_state_t;
static pulse_state_t pulse_state;

#define FSK_EST_RATIO	32	// Constant for slowness of FSK estimators
#define FSK_DEFAULT_FM_DELTA	8000	// Default estimate for frequency delta

int detect_pulse_package(const int16_t *envelope_data, const int16_t *fm_data, uint32_t len, int16_t level_limit, uint32_t samp_rate, pulse_data_t *pulses, pulse_data_t *fsk_pulses) {
	const unsigned int samples_per_ms = samp_rate / 1000;
	const int16_t HYSTERESIS = level_limit / 8;	// ±12%
	pulse_state_t *s = &pulse_state;

	// Process all new samples
	while(s->data_counter < len) {
		switch (s->state) {
			case PD_STATE_IDLE:
				s->pulse_length = 0;
				s->max_pulse = 0;
				if (envelope_data[s->data_counter] > (level_limit + HYSTERESIS)) {
					s->fsk_pulse_length = 0;
					s->fm_base_est = 0;						// FM low frequency may be everywhere
					s->fm_delta_est = FSK_DEFAULT_FM_DELTA;	// FM delta default estimate
					s->state_fsk = PD_STATE_FSK_HIGH;		// Base frequency = high pulse
					s->state = PD_STATE_PULSE;
				}
				break;
			case PD_STATE_PULSE:
				s->pulse_length++;

				// End of pulse detected?
				if (envelope_data[s->data_counter] < (level_limit - HYSTERESIS)) {	// Gap?
					pulses->pulse[pulses->num_pulses] = s->pulse_length;	// Store pulse width
					s->max_pulse = max(s->pulse_length, s->max_pulse);	// Find largest pulse

					// EOP if FSK modulation detected within pulse
					if(fsk_pulses->num_pulses > PD_MIN_PULSES) {
						if(s->state_fsk == PD_STATE_FSK_HIGH) {
							fsk_pulses->pulse[fsk_pulses->num_pulses] = s->fsk_pulse_length;	// Store last pulse
							fsk_pulses->gap[fsk_pulses->num_pulses] = 0;	// Zero gap at end
						} else {
							fsk_pulses->gap[fsk_pulses->num_pulses] = s->fsk_pulse_length;	// Store last gap
						}
						fsk_pulses->num_pulses++;
						s->state = PD_STATE_IDLE;
						return 2;	// Signal FSK package
					} else {
						fsk_pulses->num_pulses = 0;		// Clear pulses (should be more effective...)
					}
					s->pulse_length = 0;
					s->state = PD_STATE_GAP;
				} else {
					// FSK demodulation is only relevant when a carrier is present
					s->fsk_pulse_length++;
					int16_t fm_n = fm_data[s->data_counter];		// Get current FM sample
					int16_t fm_delta = abs(fm_n - s->fm_base_est);	// Get delta from base frequency estimate
					int16_t fm_hyst = fm_delta / 8; 				// ±12% hysteresis
					switch(s->state_fsk) {
						case PD_STATE_FSK_HIGH:
							if (s->pulse_length < PD_MIN_PULSE_SAMPLES) {		// Initial samples in OOK pulse?
								s->fm_base_est = s->fm_base_est - s->fm_base_est/2 + fm_n/2;	// Quick initial estimator
							} else if (fm_delta < (s->fm_delta_est/2 - fm_hyst)) {	// Freq offset below delta threshold?
								s->fm_base_est = s->fm_base_est - s->fm_base_est/FSK_EST_RATIO + fm_n/FSK_EST_RATIO;	// Slow estimator
							} else {	// Above threshold
								fsk_pulses->pulse[fsk_pulses->num_pulses] = s->fsk_pulse_length;	// Store pulse width
								s->fsk_pulse_length = 0;
								s->state_fsk = PD_STATE_FSK_LOW;
							}
							break;
						case PD_STATE_FSK_LOW:
							if (fm_delta > (s->fm_delta_est/2 + fm_hyst)) {	// Freq offset above delta threshold?
								s->fm_delta_est = s->fm_delta_est - s->fm_delta_est/FSK_EST_RATIO + fm_delta/FSK_EST_RATIO;	// Slow estimator
							} else {	// Below threshold
								fsk_pulses->gap[fsk_pulses->num_pulses] = s->fsk_pulse_length;	// Store gap width
								fsk_pulses->num_pulses++;
								s->fsk_pulse_length = 0;
								s->state_fsk = PD_STATE_FSK_HIGH;
							}
							break;
						default:
							fprintf(stderr, "pulse_demod(): Unknown FSK state!!\n");
							s->state_fsk = PD_STATE_FSK_HIGH;
					} // switch(s->state_fsk)
				} // if
				break;
			case PD_STATE_GAP:
				s->pulse_length++;
				// New pulse detected?
				if (envelope_data[s->data_counter] > (level_limit + HYSTERESIS)) {	// New pulse?
					pulses->gap[pulses->num_pulses] = s->pulse_length;	// Store gap width
					pulses->num_pulses++;	// Next pulse

					// EOP if too many pulses
					if (pulses->num_pulses >= PD_MAX_PULSES) {
						s->state = PD_STATE_IDLE;
						return 1;	// End Of Package!!
					}

					s->pulse_length = 0;
					s->fsk_pulse_length = 0;
					s->fm_base_est = 0;						// FM low frequency may be everywhere
					s->fm_delta_est = FSK_DEFAULT_FM_DELTA;	// FM delta default estimate
					s->state_fsk = PD_STATE_FSK_HIGH;		// Base frequency = high pulse
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
		// Level statistics
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
		fprintf(stderr, " [%2u] mean: %4u [%u;%u],\t count: %3u\n", n, 
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

	fprintf(stderr, "Analyzing pulses...\n");
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

