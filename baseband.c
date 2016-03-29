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

#include "baseband.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


static uint16_t scaled_squares[256];

/* precalculate lookup table for envelope detection */
static void calc_squares() {
    int i;
    for (i = 0; i < 256; i++)
        scaled_squares[i] = (127 - i) * (127 - i);
}

/** This will give a noisy envelope of OOK/ASK signals
 *  Subtract the bias (-128) and get an envelope estimation
 *  The output will be written in the input buffer
 *  @returns   pointer to the input buffer
 */
void envelope_detect(const uint8_t *iq_buf, uint16_t *y_buf, uint32_t len) {
    unsigned int i;
    for (i = 0; i < len; i++) {
        y_buf[i] = scaled_squares[iq_buf[2 * i ]] + scaled_squares[iq_buf[2 * i + 1]];
    }
}


/** Something that might look like a IIR lowpass filter
 *
 *  [b,a] = butter(1, Wc) # low pass filter with cutoff pi*Wc radians
 *  Q1.15*Q15.0 = Q16.15
 *  Q16.15>>1 = Q15.14
 *  Q15.14 + Q15.14 + Q15.14 could possibly overflow to 17.14
 *  but the b coeffs are small so it wont happen
 *  Q15.14>>14 = Q15.0 \o/
 */
#define F_SCALE 15
#define S_CONST (1<<F_SCALE)
#define FIX(x) ((int)(x*S_CONST))

///  [b,a] = butter(1, 0.01) -> 3x tau (95%) ~100 samples
//static int a[FILTER_ORDER + 1] = {FIX(1.00000), FIX(0.96907)};
//static int b[FILTER_ORDER + 1] = {FIX(0.015466), FIX(0.015466)};
///  [b,a] = butter(1, 0.05) -> 3x tau (95%) ~20 samples
static int a[FILTER_ORDER + 1] = {FIX(1.00000), FIX(0.85408)};
static int b[FILTER_ORDER + 1] = {FIX(0.07296), FIX(0.07296)};


void baseband_low_pass_filter(const uint16_t *x_buf, int16_t *y_buf, uint32_t len, FilterState *state) {
    unsigned int i;
    // Fixme: Will Segmentation Fault if len < FILTERORDER

    /* Calculate first sample */
    y_buf[0] = ((a[1] * state->y[0] >> 1) + (b[0] * x_buf[0] >> 1) + (b[1] * state->x[0] >> 1)) >> (F_SCALE - 1);
    for (i = 1; i < len; i++) {
        y_buf[i] = ((a[1] * y_buf[i - 1] >> 1) + (b[0] * x_buf[i] >> 1) + (b[1] * x_buf[i - 1] >> 1)) >> (F_SCALE - 1);
    }

    /* Save last samples */
    memcpy(state->x, &x_buf[len - FILTER_ORDER], FILTER_ORDER * sizeof (int16_t));
    memcpy(state->y, &y_buf[len - FILTER_ORDER], FILTER_ORDER * sizeof (int16_t));
    //fprintf(stderr, "%d\n", y_buf[0]);
}


/// Integer implementation of atan2() with int16_t normalized output
///
/// Returns arc tangent of y/x across all quadrants in radians
/// Reference: http://dspguru.com/dsp/tricks/fixed-point-atan2-with-self-normalization
/// @param y: Numerator (imaginary value of complex vector)
/// @param x: Denominator (real value of complex vector)
/// @return angle in radians (Pi equals INT16_MAX)
int16_t atan2_int16(int16_t y, int16_t x) {
	static const int32_t I_PI_4 = INT16_MAX/4;		// M_PI/4
	static const int32_t I_3_PI_4 = 3*INT16_MAX/4;	// 3*M_PI/4
	const int32_t abs_y = abs(y);
	int32_t r, angle;

	if (x >= 0) {	// Quadrant I and IV
		int32_t denom = (abs_y + x);
		if (denom == 0) denom = 1;	// Prevent divide by zero
		r = ((x - abs_y) << 16) / denom;
		angle = I_PI_4;
	} else {		// Quadrant II and III
		int32_t denom = (abs_y - x);
		if (denom == 0) denom = 1;	// Prevent divide by zero
		r = ((x + abs_y) << 16) / denom;
		angle = I_3_PI_4;
	}
	angle -= (I_PI_4 * r) >> 16;	// Error max 0.07 radians
	if (y < 0) angle = -angle;	// Negate if in III or IV
	return angle;
}


///  [b,a] = butter(1, 0.1) -> 3x tau (95%) ~10 samples
//static int alp[2] = {FIX(1.00000), FIX(0.72654)};
//static int blp[2] = {FIX(0.13673), FIX(0.13673)};
///  [b,a] = butter(1, 0.2) -> 3x tau (95%) ~5 samples
static int alp[2] = {FIX(1.00000), FIX(0.50953)};
static int blp[2] = {FIX(0.24524), FIX(0.24524)};

void baseband_demod_FM(const uint8_t *x_buf, int16_t *y_buf, unsigned num_samples, DemodFM_State *state) {
	int16_t ar, ai;		// New IQ sample: x[n]
	int16_t br, bi;		// Old IQ sample: x[n-1]
	int32_t pr, pi;		// Phase difference vector
	int16_t angle;		// Phase difference angle
	int16_t xlp, ylp, xlp_old, ylp_old;	// Low Pass filter variables

	// Pre-feed old sample
	ar = state->br; ai = state->bi;
	xlp_old = state->xlp; ylp_old = state->ylp;

	for (unsigned n = 0; n < num_samples; n++) {
		// delay old sample 
		br = ar;
		bi = ai;
		// get new sample
		ar = x_buf[2*n]-128;
		ai = x_buf[2*n+1]-128;
		// Calculate phase difference vector: x[n] * conj(x[n-1])
		pr = ar*br+ai*bi;	// May exactly overflow an int16_t (-128*-128 + -128*-128)
		pi = ai*br-ar*bi; 
//		xlp = (int16_t)((atan2f(pi, pr) / M_PI) * INT16_MAX);	// Floating point implementation
		xlp = atan2_int16(pi, pr);	// Integer implementation
//		xlp = pi;					// Cheat and use only imaginary part (works OK, but is amplitude sensitive)
		// Low pass filter
		ylp = ((alp[1] * ylp_old >> 1) + (blp[0] * xlp >> 1) + (blp[1] * xlp_old >> 1)) >> (F_SCALE - 1);
		ylp_old = ylp; xlp_old = xlp;
		y_buf[n] = ylp;
	}

	// Store newest sample for next run
	state->br = ar; state->bi = ai;
	state->xlp = xlp_old; state->ylp = ylp_old;
}


void baseband_init(void) {
	calc_squares();
}


static FILE *dumpfile = NULL;

void baseband_dumpfile(const uint8_t *buf, uint32_t len) {
	if (dumpfile == NULL) {
		dumpfile = fopen("dumpfile.dat", "wb");
	}
	
	if (dumpfile == NULL) {
		fprintf(stderr, "Error: could not open dumpfile.dat\n");
	} else {
		fwrite(buf, 1, len, dumpfile);
		fflush(dumpfile);		// Flush as file is not closed cleanly...
	}
}
