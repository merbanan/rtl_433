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
#include "pulse_demod.h"
#include "util.h"
#include "rtl_433.h"
#include <limits.h>
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


// OOK adaptive level estimator constants
#define OOK_HIGH_LOW_RATIO	8			// Default ratio between high and low (noise) level
#define OOK_MIN_HIGH_LEVEL	1000		// Minimum estimate of high level
#define OOK_MAX_HIGH_LEVEL	(128*128)	// Maximum estimate for high level (A unit phasor is 128, anything above is overdrive)
#define OOK_MAX_LOW_LEVEL	(OOK_MAX_HIGH_LEVEL/2)	// Maximum estimate for low level
#define OOK_EST_RATIO		64			// Constant for slowness of OOK estimators

// FSK adaptive frequency estimator constants
#define FSK_DEFAULT_FM_DELTA	6000	// Default estimate for frequency delta
#define FSK_EST_RATIO		32			// Constant for slowness of FSK estimators


/// Internal state data for pulse_FSK_detect()
typedef struct {
	unsigned int fsk_pulse_length;		// Counter for internal FSK pulse detection
	enum {
		PD_STATE_FSK_INIT	= 0,	// Initial frequency estimation
		PD_STATE_FSK_F1		= 1,	// High frequency (pulse)
		PD_STATE_FSK_F2		= 2,	// Low frequency (gap)
		PD_STATE_FSK_ERROR	= 3		// Error - stay here until cleared
	} state_fsk;

	int fm_f1_est;			// Estimate for the F1 frequency for FSK
	int fm_f2_est;			// Estimate for the F2 frequency for FSK

} pulse_FSK_state_t;


/// Demodulate Frequency Shift Keying (FSK) sample by sample
///
/// Function is stateful between calls
/// Builds estimate for initial frequency. When frequency deviates more than a
/// threshold value it will determine whether the deviation is positive or negative
/// to classify it as a pulse or gap. It will then transition to other state (F1 or F2)
/// and build an estimate of the other frequency. It will then transition back and forth when current
/// frequency is closer to other frequency estimate.
/// Includes spurious suppression by coalescing pulses when pulse/gap widths are too short.
/// Pulses equal higher frequency (F1) and Gaps equal lower frequency (F2)
/// @param fm_n: One single sample of FM data
/// @param *fsk_pulses: Will return a pulse_data_t structure for FSK demodulated data
/// @param *s: Internal state
void pulse_FSK_detect(int16_t fm_n, pulse_data_t *fsk_pulses, pulse_FSK_state_t *s) {
	const int fm_f1_delta = abs(fm_n - s->fm_f1_est);	// Get delta from F1 frequency estimate
	const int fm_f2_delta = abs(fm_n - s->fm_f2_est);	// Get delta from F2 frequency estimate
	s->fsk_pulse_length++;

	switch(s->state_fsk) {
		case PD_STATE_FSK_INIT:		// Initial frequency - High or low?
			// Initial samples?
			if (s->fsk_pulse_length < PD_MIN_PULSE_SAMPLES) {
				s->fm_f1_est = s->fm_f1_est/2 + fm_n/2;		// Quick initial estimator
			// Above default frequency delta?
			} else if (fm_f1_delta > (FSK_DEFAULT_FM_DELTA/2)) {
				// Positive frequency delta - Initial frequency was low (gap)
				if (fm_n > s->fm_f1_est) {
					s->state_fsk = PD_STATE_FSK_F1;
					s->fm_f2_est = s->fm_f1_est;	// Switch estimates
					s->fm_f1_est = fm_n;			// Prime F1 estimate
					fsk_pulses->pulse[0] = 0;		// Initial frequency was a gap...
					fsk_pulses->gap[0] = s->fsk_pulse_length;		// Store gap width
					fsk_pulses->num_pulses++;
					s->fsk_pulse_length = 0;
				// Negative Frequency delta - Initial frequency was high (pulse)
				} else {
					s->state_fsk = PD_STATE_FSK_F2;
					s->fm_f2_est = fm_n;	// Prime F2 estimate
					fsk_pulses->pulse[0] = s->fsk_pulse_length;	// Store pulse width
					s->fsk_pulse_length = 0;
				}
			// Still below threshold
			} else {
				s->fm_f1_est += fm_n/FSK_EST_RATIO - s->fm_f1_est/FSK_EST_RATIO;	// Slow estimator
			}
			break;
		case PD_STATE_FSK_F1:		// Pulse high at F1 frequency
			// Closer to F2 than F1?
			if (fm_f1_delta > fm_f2_delta) {
				s->state_fsk = PD_STATE_FSK_F2;
				// Store if pulse is not too short (suppress spurious)
				if (s->fsk_pulse_length >= PD_MIN_PULSE_SAMPLES) {
					fsk_pulses->pulse[fsk_pulses->num_pulses] = s->fsk_pulse_length;	// Store pulse width
					s->fsk_pulse_length = 0;
				// Else rewind to last gap
				} else {
					s->fsk_pulse_length += fsk_pulses->gap[fsk_pulses->num_pulses-1];	// Restore counter
					fsk_pulses->num_pulses--;		// Rewind one pulse
					// Are we back to initial frequency? (Was initial frequency a gap?)
					if ((fsk_pulses->num_pulses == 0) && (fsk_pulses->pulse[0] == 0)) {
						s->fm_f1_est = s->fm_f2_est;	// Switch back estimates
						s->state_fsk = PD_STATE_FSK_INIT;
					}
				}
			// Still below threshold
			} else {
				s->fm_f1_est += fm_n/FSK_EST_RATIO - s->fm_f1_est/FSK_EST_RATIO;	// Slow estimator
			}
			break;
		case PD_STATE_FSK_F2:		// Pulse gap at F2 frequency
			// Freq closer to F1 than F2 ?
			if (fm_f2_delta > fm_f1_delta) {
				s->state_fsk = PD_STATE_FSK_F1;
				// Store if pulse is not too short (suppress spurious)
				if (s->fsk_pulse_length >= PD_MIN_PULSE_SAMPLES) {
					fsk_pulses->gap[fsk_pulses->num_pulses] = s->fsk_pulse_length;	// Store gap width
					fsk_pulses->num_pulses++;	// Go to next pulse
					s->fsk_pulse_length = 0;
					// When pulse buffer is full go to error state
					if (fsk_pulses->num_pulses >= PD_MAX_PULSES) {
						fprintf(stderr, "pulse_FSK_detect(): Maximum number of pulses reached!\n");
						s->state_fsk = PD_STATE_FSK_ERROR;
					}
				// Else rewind to last pulse
				} else {
					s->fsk_pulse_length += fsk_pulses->pulse[fsk_pulses->num_pulses];	// Restore counter
					// Are we back to initial frequency?
					if (fsk_pulses->num_pulses == 0) {
						s->state_fsk = PD_STATE_FSK_INIT;
					}
				}
			// Still below threshold
			} else {
				s->fm_f2_est += fm_n/FSK_EST_RATIO - s->fm_f2_est/FSK_EST_RATIO;	// Slow estimator
			}
			break;
		case PD_STATE_FSK_ERROR:		// Stay here until cleared
			break;
		default:
			fprintf(stderr, "pulse_FSK_detect(): Unknown FSK state!!\n");
			s->state_fsk = PD_STATE_FSK_ERROR;
	} // switch(s->state_fsk)
	return;
}


/// Wrap up FSK modulation and store last data at End Of Package
///
/// @param fm_n: One single sample of FM data
/// @param *fsk_pulses: Pulse_data_t structure for FSK demodulated data
/// @param *s: Internal state
void pulse_FSK_wrap_up(pulse_data_t *fsk_pulses, pulse_FSK_state_t *s) {
	if (fsk_pulses->num_pulses < PD_MAX_PULSES) {	// Avoid overflow
		s->fsk_pulse_length++;
		if(s->state_fsk == PD_STATE_FSK_F1) {
			fsk_pulses->pulse[fsk_pulses->num_pulses] = s->fsk_pulse_length;	// Store last pulse
			fsk_pulses->gap[fsk_pulses->num_pulses] = 0;	// Zero gap at end
		} else {
			fsk_pulses->gap[fsk_pulses->num_pulses] = s->fsk_pulse_length;	// Store last gap
		}
		fsk_pulses->num_pulses++;
	}
}


/// Internal state data for detect_pulse_package()
typedef struct {
	enum {
		PD_STATE_IDLE		= 0,
		PD_STATE_PULSE		= 1,
		PD_STATE_GAP		= 2
	} state;
	int pulse_length;		// Counter for internal pulse detection
	int max_pulse;			// Size of biggest pulse detected

	int data_counter;		// Counter for how much of data chunck is processed
	int lead_in_counter;	// Counter for allowing initial noise estimate to settle

	int ook_low_estimate;		// Estimate for the OOK low level (base noise level) in the envelope data
	int ook_high_estimate;		// Estimate for the OOK high level

	pulse_FSK_state_t	FSK_state;

} pulse_state_t;
static pulse_state_t pulse_state;


int detect_pulse_package(const int16_t *envelope_data, const int16_t *fm_data, int len, int16_t level_limit, uint32_t samp_rate, pulse_data_t *pulses, pulse_data_t *fsk_pulses) {
	const int samples_per_ms = samp_rate / 1000;
	pulse_state_t *s = &pulse_state;
	s->ook_high_estimate = max(s->ook_high_estimate, OOK_MIN_HIGH_LEVEL);	// Be sure to set initial minimum level

	// Process all new samples
	while(s->data_counter < len) {
		// Calculate OOK detection threshold and hysteresis
		const int16_t am_n = envelope_data[s->data_counter];
		int16_t ook_threshold = s->ook_low_estimate + (s->ook_high_estimate - s->ook_low_estimate) / 2;
		if (level_limit != 0) ook_threshold = level_limit;	// Manual override
		const int16_t ook_hysteresis = ook_threshold / 8;	// ±12%

		// OOK State machine
		switch (s->state) {
			case PD_STATE_IDLE:
				if (am_n > (ook_threshold + ook_hysteresis)	// Above threshold?
				 && s->lead_in_counter > OOK_EST_RATIO		// Lead in counter to stabilize noise estimate
				 ) {
					// Initialize all data
					pulse_data_clear(pulses);
					pulse_data_clear(fsk_pulses);
					s->pulse_length = 0;
					s->max_pulse = 0;
					s->FSK_state = (pulse_FSK_state_t){0};
					s->state = PD_STATE_PULSE;
				} else {	// We are still idle..
					// Estimate low (noise) level
					s->ook_low_estimate += am_n / OOK_EST_RATIO - s->ook_low_estimate / OOK_EST_RATIO;
					// Calculate default OOK high level estimate
					s->ook_high_estimate = OOK_HIGH_LOW_RATIO * s->ook_low_estimate;	// Default is a ratio of low level
					s->ook_high_estimate = max(s->ook_high_estimate, OOK_MIN_HIGH_LEVEL);
					s->ook_high_estimate = min(s->ook_high_estimate, OOK_MAX_HIGH_LEVEL);
					if (s->lead_in_counter <= OOK_EST_RATIO) s->lead_in_counter++;		// Allow inital estimate to settle
				}
				break;
			case PD_STATE_PULSE:
				s->pulse_length++;
				// End of pulse detected?
				if (am_n  < (ook_threshold - ook_hysteresis)) {	// Gap?
					// Check for spurious short pulses
					if (s->pulse_length < PD_MIN_PULSE_SAMPLES) {
						s->state = PD_STATE_IDLE;
					} else {
						// Continue with OOK decoding
						pulses->pulse[pulses->num_pulses] = s->pulse_length;	// Store pulse width
						s->max_pulse = max(s->pulse_length, s->max_pulse);	// Find largest pulse
						s->pulse_length = 0;
						s->state = PD_STATE_GAP;
					}
					// Determine if FSK modulation is detected
					if(fsk_pulses->num_pulses > PD_MIN_PULSES) {
						// Store last pulse/gap (FSK_state is manipulated directly...)
						pulse_FSK_wrap_up(fsk_pulses, &s->FSK_state);
						// Store estimates
						fsk_pulses->ook_low_estimate = s->ook_low_estimate;
						fsk_pulses->ook_high_estimate = s->ook_high_estimate;
						s->state = PD_STATE_IDLE;	// Ensure everything is reset
						return 2;	// FSK package detected!!!
					}
				} else {
					// Calculate OOK high level estimate
					s->ook_high_estimate += am_n / OOK_EST_RATIO - s->ook_high_estimate / OOK_EST_RATIO;	//  Slow estimator
					s->ook_high_estimate = max(s->ook_high_estimate, OOK_MIN_HIGH_LEVEL);
					s->ook_high_estimate = min(s->ook_high_estimate, OOK_MAX_HIGH_LEVEL);

					// **** Start of FSK Demodulation ****
					if(pulses->num_pulses == 0) {	// Only during first pulse
						pulse_FSK_detect(fm_data[s->data_counter], fsk_pulses, &s->FSK_state);
					}
					// **** End of FSK Demodulation ****
				} // if
				break;
			case PD_STATE_GAP:
				s->pulse_length++;
				// New pulse detected?
				if (am_n  > (ook_threshold + ook_hysteresis)) {	// New pulse?
					pulses->gap[pulses->num_pulses] = s->pulse_length;	// Store gap width
					pulses->num_pulses++;	// Next pulse

					// EOP if too many pulses
					if (pulses->num_pulses >= PD_MAX_PULSES) {
						s->state = PD_STATE_IDLE;
						// Store estimates
						pulses->ook_low_estimate = s->ook_low_estimate;
						pulses->ook_high_estimate = s->ook_high_estimate;
						return 1;	// End Of Package!!
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
					// Store estimates
					pulses->ook_low_estimate = s->ook_low_estimate;
					pulses->ook_high_estimate = s->ook_high_estimate;
					return 1;	// End Of Package!!
				}
				break;
			default:
				fprintf(stderr, "demod_OOK(): Unknown state!!\n");
				s->state = PD_STATE_IDLE;
		} // switch
		s->data_counter++;
	} // while

	s->data_counter = 0;
	return 0;	// Out of data
}


#define MAX_HIST_BINS 16

/// Histogram data for single bin
typedef struct {
	unsigned count;
	int sum;
	int mean;
	int min;
	int max;
} hist_bin_t;

/// Histogram data for all bins
typedef struct {
	unsigned bins_count;
	hist_bin_t bins[MAX_HIST_BINS];
} histogram_t;


/// Generate a histogram (unsorted)
void histogram_sum(histogram_t *hist, const int *data, unsigned len, float tolerance) {
	unsigned bin;	// Iterator will be used outside for!

	for(unsigned n = 0; n < len; ++n) {
		// Search for match in existing bins
		for(bin = 0; bin < hist->bins_count; ++bin) {
			int bn = data[n];
			int bm = hist->bins[bin].mean;
			if (abs(bn - bm) < (tolerance * max(bn, bm))) {
				hist->bins[bin].count++;
				hist->bins[bin].sum += data[n];
				hist->bins[bin].mean = hist->bins[bin].sum / hist->bins[bin].count;
				hist->bins[bin].min	= min(data[n], hist->bins[bin].min);
				hist->bins[bin].max	= max(data[n], hist->bins[bin].max);
				break;	// Match found! Data added to existing bin
			}
		}
		// No match found? Add new bin
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


/// Delete bin from histogram
void histogram_delete_bin(histogram_t *hist, unsigned index) {
	const hist_bin_t	zerobin = {0};
	if (hist->bins_count < 1) return;	// Avoid out of bounds
	// Move all bins afterwards one forward
	for(unsigned n = index; n < hist->bins_count-1; ++n) {
		hist->bins[n] = hist->bins[n+1];
	}
	hist->bins_count--;
	hist->bins[hist->bins_count] = zerobin;	// Clear previously last bin
}


/// Swap two bins in histogram
void histogram_swap_bins(histogram_t *hist, unsigned index1, unsigned index2) {
	hist_bin_t	tempbin;
	if ((index1 < hist->bins_count) && (index2 < hist->bins_count)) {		// Avoid out of bounds
		tempbin = hist->bins[index1];
		hist->bins[index1] = hist->bins[index2];
		hist->bins[index2] = tempbin;
	}
}


/// Sort histogram with mean value (order lowest to highest)
void histogram_sort_mean(histogram_t *hist) {
	if (hist->bins_count < 2) return;		// Avoid underflow
	// Compare all bins (bubble sort)
	for(unsigned n = 0; n < hist->bins_count-1; ++n) {
		for(unsigned m = n+1; m < hist->bins_count; ++m) {
			if (hist->bins[m].mean < hist->bins[n].mean) {
				histogram_swap_bins(hist, m, n);
			} // if 
		} // for m
	} // for n
}


/// Sort histogram with count value (order lowest to highest)
void histogram_sort_count(histogram_t *hist) {
	if (hist->bins_count < 2) return;		// Avoid underflow
	// Compare all bins (bubble sort)
	for(unsigned n = 0; n < hist->bins_count-1; ++n) {
		for(unsigned m = n+1; m < hist->bins_count; ++m) {
			if (hist->bins[m].count < hist->bins[n].count) {
				histogram_swap_bins(hist, m, n);
			} // if 
		} // for m
	} // for n
}


/// Fuse histogram bins with means within tolerance
void histogram_fuse_bins(histogram_t *hist, float tolerance) {
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
				histogram_delete_bin(hist, m);
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
void pulse_analyzer(pulse_data_t *data)
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
	fprintf(stderr, "Level estimates (signal / noise): %i / %i\n", data->ook_high_estimate, data->ook_low_estimate);

	fprintf(stderr, "Guessing modulation: ");
	struct protocol_state device = { .name = "Analyzer Device", 0};
	histogram_sort_mean(&hist_pulses);	// Easier to work with sorted data
	histogram_sort_mean(&hist_gaps);
	if(data->num_pulses == 1) {
		fprintf(stderr, "Single pulse detected. Probably Frequency Shift Keying or just noise...\n");
	} else if(hist_pulses.bins_count == 1 && hist_gaps.bins_count == 1) {
		fprintf(stderr, "Un-modulated signal. Maybe a preamble...\n");
	} else if(hist_pulses.bins_count == 1 && hist_gaps.bins_count > 1) {
		fprintf(stderr, "Pulse Position Modulation with fixed pulse width\n");
		device.modulation	= OOK_PULSE_PPM_RAW;
		device.short_limit	= (hist_gaps.bins[0].mean + hist_gaps.bins[1].mean) / 2;	// Set limit between two lowest gaps
		device.long_limit	= hist_gaps.bins[1].max + 1;								// Set limit above next lower gap
		device.reset_limit	= hist_gaps.bins[hist_gaps.bins_count-1].max + 1;			// Set limit above biggest gap
	} else if(hist_pulses.bins_count == 2 && hist_gaps.bins_count == 1) {
		fprintf(stderr, "Pulse Width Modulation with fixed gap\n");
		device.modulation	= OOK_PULSE_PWM_RAW;
		device.short_limit	= (hist_pulses.bins[0].mean + hist_pulses.bins[1].mean) / 2;	// Set limit between two pulse widths
		device.long_limit	= hist_gaps.bins[hist_gaps.bins_count-1].max + 1;				// Set limit above biggest gap
		device.reset_limit	= device.long_limit;
	} else if(hist_pulses.bins_count == 2 && hist_gaps.bins_count == 2 && hist_periods.bins_count == 1) {
		fprintf(stderr, "Pulse Width Modulation with fixed period\n");
		device.modulation	= OOK_PULSE_PWM_RAW;
		device.short_limit	= (hist_pulses.bins[0].mean + hist_pulses.bins[1].mean) / 2;	// Set limit between two pulse widths
		device.long_limit	= hist_gaps.bins[hist_gaps.bins_count-1].max + 1;				// Set limit above biggest gap
		device.reset_limit	= device.long_limit;
	} else if(hist_pulses.bins_count == 2 && hist_gaps.bins_count == 2 && hist_periods.bins_count == 3) {
		fprintf(stderr, "Manchester coding\n");
		device.modulation	= OOK_PULSE_MANCHESTER_ZEROBIT;
		device.short_limit	= min(hist_pulses.bins[0].mean, hist_pulses.bins[1].mean);		// Assume shortest pulse is half period
		device.long_limit	= 0; // Not used
		device.reset_limit	= hist_gaps.bins[hist_gaps.bins_count-1].max + 1;				// Set limit above biggest gap
	} else if(hist_pulses.bins_count == 3) {
		fprintf(stderr, "Pulse Width Modulation with startbit/delimiter\n");
		device.modulation	= OOK_PULSE_PWM_TERNARY;
		device.short_limit	= (hist_pulses.bins[0].mean + hist_pulses.bins[1].mean) / 2;	// Set limit between two lowest pulse widths
		device.long_limit	= (hist_pulses.bins[1].mean + hist_pulses.bins[2].mean) / 2;	// Set limit between two next lowest pulse widths
		device.reset_limit	= hist_gaps.bins[hist_gaps.bins_count-1].max  +1;				// Set limit above biggest gap
		// Re-sort to find lowest pulse count index (is probably delimiter)
		histogram_sort_count(&hist_pulses);
		if		(hist_pulses.bins[0].mean < device.short_limit)	{	device.demod_arg = 0; }	// Shortest pulse is delimiter
		else if	(hist_pulses.bins[0].mean < device.long_limit)	{	device.demod_arg = 1; }	// Middle pulse is delimiter
		else													{	device.demod_arg = 2; }	// Longest pulse is delimiter
	} else {
		fprintf(stderr, "No clue...\n");
	}

	if(device.modulation) {
		fprintf(stderr, "Attempting demodulation... short_limit: %u, long_limit: %u, reset_limit: %u, demod_arg: %lu\n", 
			device.short_limit, device.long_limit, device.reset_limit, device.demod_arg);
		data->gap[data->num_pulses-1] = device.reset_limit + 1;	// Be sure to terminate package
		switch(device.modulation) {
//			case OOK_PULSE_PCM_RZ:
//				pulse_demod_pcm(data, &device);
//				break;
			case OOK_PULSE_PPM_RAW:
				pulse_demod_ppm(data, &device);
				break;
			case OOK_PULSE_PWM_RAW:
				pulse_demod_pwm(data, &device);
				break;
			case OOK_PULSE_PWM_TERNARY:
				pulse_demod_pwm_ternary(data, &device);
				break;
			case OOK_PULSE_MANCHESTER_ZEROBIT:
				pulse_demod_manchester_zerobit(data, &device);
				break;
			default:
				fprintf(stderr, "Unsupported\n");
		}
	}

	fprintf(stderr, "\n");
}

