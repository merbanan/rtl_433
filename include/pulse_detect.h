/**
 * Pulse detection functions
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef INCLUDE_PULSE_DETECT_H_
#define INCLUDE_PULSE_DETECT_H_

#include <stdint.h>

#define PD_MAX_PULSES 1200			// Maximum number of pulses before forcing End Of Package
#define PD_MIN_PULSES 16			// Minimum number of pulses before declaring a proper package
#define PD_MIN_PULSE_SAMPLES 10		// Minimum number of samples in a pulse for proper detection
#define PD_MIN_GAP_MS 10			// Minimum gap size in milliseconds to exceed to declare End Of Package
#define PD_MAX_GAP_MS 100			// Maximum gap size in milliseconds to exceed to declare End Of Package
#define PD_MAX_GAP_RATIO 10			// Ratio gap/pulse width to exceed to declare End Of Package (heuristic)
#define PD_MAX_PULSE_MS 100			// Pulse width in ms to exceed to declare End Of Package (e.g. for non OOK packages)

/// Data for a compact representation of generic pulse train
typedef struct {
	unsigned int num_pulses;
	int pulse[PD_MAX_PULSES];	// Contains width of a pulse	(high)
	int gap[PD_MAX_PULSES];		// Width of gaps between pulses (low)
	int ook_low_estimate;		// Estimate for the OOK low level (base noise level) at beginning of package
	int ook_high_estimate;		// Estimate for the OOK high level at end of package
	int fsk_f1_est;				// Estimate for the F1 frequency for FSK
	int fsk_f2_est;				// Estimate for the F2 frequency for FSK
} pulse_data_t;


/// Clear the content of a pulse_data_t structure
void pulse_data_clear(pulse_data_t *data);		// Clear the struct

/// Print the content of a pulse_data_t structure (for debug)
void pulse_data_print(const pulse_data_t *data);


/// Demodulate On/Off Keying (OOK) and Frequency Shift Keying (FSK) from an envelope signal
///
/// Function is stateful and can be called with chunks of input data
/// @param envelope_data: Samples with amplitude envelope of carrier 
/// @param fm_data: Samples with frequency offset from center frequency
/// @param len: Number of samples in input buffers
/// @param samp_rate: Sample rate in samples per second
/// @param *pulses: Will return a pulse_data_t structure
/// @param *fsk_pulses: Will return a pulse_data_t structure for FSK demodulated data
/// @return 0 if all input sample data is processed
/// @return 1 if OOK package is detected (but all sample data is still not completely processed)
/// @return 2 if FSK package is detected (but all sample data is still not completely processed)
int pulse_detect_package(const int16_t *envelope_data, const int16_t *fm_data, int len, int16_t level_limit, uint32_t samp_rate, pulse_data_t *pulses, pulse_data_t *fsk_pulses);


/// Analyze and print result
void pulse_analyzer(pulse_data_t *data, uint32_t samp_rate);


#endif /* INCLUDE_PULSE_DETECT_H_ */
