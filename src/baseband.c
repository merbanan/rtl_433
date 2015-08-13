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
#include <stdio.h>
#include <string.h>


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
void envelope_detect(unsigned char *buf, uint32_t len, int decimate) {
    uint16_t* sample_buffer = (uint16_t*) buf;
    unsigned int i;
    unsigned op = 0;
    unsigned int stride = 1 << decimate;

    for (i = 0; i < len / 2; i += stride) {
        sample_buffer[op++] = scaled_squares[buf[2 * i ]] + scaled_squares[buf[2 * i + 1]];
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

static uint16_t lp_xmem[FILTER_ORDER] = {0};

///  [b,a] = butter(1, 0.01) -> 3x tau (95%) ~100 samples
//static int a[FILTER_ORDER + 1] = {FIX(1.00000), FIX(0.96907)};
//static int b[FILTER_ORDER + 1] = {FIX(0.015466), FIX(0.015466)};
///  [b,a] = butter(1, 0.05) -> 3x tau (95%) ~20 samples
static int a[FILTER_ORDER + 1] = {FIX(1.00000), FIX(0.85408)};
static int b[FILTER_ORDER + 1] = {FIX(0.07296), FIX(0.07296)};

void low_pass_filter(uint16_t *x_buf, int16_t *y_buf, uint32_t len) {
    unsigned int i;

    /* Calculate first sample */
    y_buf[0] = ((a[1] * y_buf[-1] >> 1) + (b[0] * x_buf[0] >> 1) + (b[1] * lp_xmem[0] >> 1)) >> (F_SCALE - 1);
    for (i = 1; i < len; i++) {
        y_buf[i] = ((a[1] * y_buf[i - 1] >> 1) + (b[0] * x_buf[i] >> 1) + (b[1] * x_buf[i - 1] >> 1)) >> (F_SCALE - 1);
    }

    /* Save last sample */
    memcpy(lp_xmem, &x_buf[len - 1 - FILTER_ORDER], FILTER_ORDER * sizeof (int16_t));
    memcpy(&y_buf[-FILTER_ORDER], &y_buf[len - 1 - FILTER_ORDER], FILTER_ORDER * sizeof (int16_t));
    //fprintf(stderr, "%d\n", y_buf[0]);
}


void baseband_init(void) {
	calc_squares();
}


static FILE *dumpfile = NULL;

void baseband_dumpfile(uint8_t *buf, uint32_t len) {
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
