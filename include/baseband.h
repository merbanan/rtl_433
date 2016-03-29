/**
 * Baseband
 * 
 * Various functions for baseband sample processing
 *
 * Copyright (C) 2012 by Benjamin Larsson <benjamin@southpole.se>
 * Copyright (C) 2015 Tommy Vestermark
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef INCLUDE_BASEBAND_H_
#define INCLUDE_BASEBAND_H_

#include <stdint.h>

/// This will give a noisy envelope of OOK/ASK signals

/// Subtract the bias (-128) and get an envelope estimation (absolute squared)
/// @param *iq_buf: input samples (I/Q samples in interleaved uint8)
/// @param *y_buf: output 
/// @param len: number of samples to process
void envelope_detect(const uint8_t *iq_buf, uint16_t *y_buf, uint32_t len);

#define FILTER_ORDER 1

/// Filter state buffer
typedef struct {
	int16_t	y[FILTER_ORDER];
	int16_t	x[FILTER_ORDER];
} FilterState;

/// FM_Demod state buffer
typedef struct {
	int16_t	br, bi;		// Last I/Q sample
	int16_t xlp, ylp;	// Low-pass filter state
} DemodFM_State;

/// Lowpass filter
///
/// Function is stateful
/// @param *x_buf: input samples to be filtered
/// @param *y_buf: output from filter
/// @param len: number of samples to process
/// @param FilterState: State to store between chunk processing
void baseband_low_pass_filter(const uint16_t *x_buf, int16_t *y_buf, uint32_t len, FilterState *state);

/// FM demodulator
///
/// Function is stateful
/// @param *x_buf: input samples (I/Q samples in interleaved uint8)
/// @param *y_buf: output from FM demodulator
/// @param len: number of samples to process
/// @param DemodFM_State: State to store between chunk processing
void baseband_demod_FM(const uint8_t *x_buf, int16_t *y_buf, unsigned num_samples, DemodFM_State *state);

/// Initialize tables and constants
/// Should be called once at startup
void baseband_init(void);

/// Dump binary data (for debug purposes)
void baseband_dumpfile(const uint8_t *buf, uint32_t len);

#endif /* INCLUDE_BASEBAND_H_ */
