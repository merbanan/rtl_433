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

#define PD_MAX_PULSES 1000			// Maximum number of pulses before forcing End Of Package
#define PD_MIN_GAP_MS 10			// Minimum gap size in milliseconds to exceed to declare End Of Package
#define PD_MAX_GAP_MS 100			// Maximum gap size in milliseconds to exceed to declare End Of Package
#define PD_MAX_GAP_RATIO 10			// Ratio gap/pulse width to exceed to declare End Of Package (heuristic)
#define PD_MAX_PULSE_MS 100			// Pulse width in ms to exceed to declare End Of Package (e.g. for non OOK packages)

/// Data for a compact representation of generic pulse train
typedef struct {
	unsigned int num_pulses;
	unsigned int pulse[PD_MAX_PULSES];	// Contains width of a pulse	(high)
	unsigned int gap[PD_MAX_PULSES];	// Width of gaps between pulses (low)
} pulse_data_t;


/// Clear the content of a pulse_data_t structure
void pulse_data_clear(pulse_data_t *data);		// Clear the struct

/// Print the content of a pulse_data_t structure (for debug)
void pulse_data_print(const pulse_data_t *data);


/// Demodulate On/Off Keying from an envelope signal
///
/// Function is stateful and can be called with chunks of input data
/// @param samp_rate: Sample rate in samples per second
/// @param *pulses: Will return a pulse_data_t structure
/// @return 0 if all input data is processed
/// @return 1 if package is detected (but data is still not completely processed)
int detect_pulse_package(const int16_t *envelope_data, uint32_t len, int16_t level_limit, uint32_t samp_rate, pulse_data_t *pulses);


/// Analyze and print result
void pulse_analyzer(const pulse_data_t *data);


#endif /* INCLUDE_PULSE_DETECT_H_ */
