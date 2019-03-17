/** @file
    Various functions for baseband sample processing.

    Copyright (C) 2012 by Benjamin Larsson <benjamin@southpole.se>
    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "baseband.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint16_t scaled_squares[256];

/** precalculate lookup table for envelope detection. */
static void calc_squares()
{
    int i;
    for (i = 0; i < 256; i++)
        scaled_squares[i] = (127 - i) * (127 - i);
}

/** This will give a noisy envelope of OOK/ASK signals.
    Subtract the bias (-128) and get an envelope estimation
    The output will be written in the input buffer
    @returns   pointer to the input buffer
*/
void envelope_detect(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len)
{
    unsigned long i;
    for (i = 0; i < len; i++) {
        y_buf[i] = scaled_squares[iq_buf[2 * i ]] + scaled_squares[iq_buf[2 * i + 1]];
    }
}

/** This will give a noisy envelope of OOK/ASK signals.
    Subtracts the bias (-128) and calculates the norm (scaled by 16384).
    Using a LUT is slower for O1 and above.
*/
void envelope_detect_nolut(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len)
{
    unsigned long i;
    for (i = 0; i < len; i++) {
        int16_t x = 127 - iq_buf[2 * i];
        int16_t y = 127 - iq_buf[2 * i + 1];
        y_buf[i]  = x * x + y * y; // max 32768, fs 16384
    }
}

/** 122/128, 51/128 Magnitude Estimator for CU8 (SIMD has min/max).
    Note that magnitude emphasizes quiet signals / deemphasizes loud signals.
*/
void magnitude_est_cu8(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len)
{
    unsigned long i;
    for (i = 0; i < len; i++) {
        uint16_t x = abs(iq_buf[2 * i] - 128);
        uint16_t y = abs(iq_buf[2 * i + 1] - 128);
        uint16_t mi = x < y ? x : y;
        uint16_t mx = x > y ? x : y;
        uint16_t mag_est = 122 * mx + 51 * mi;
        y_buf[i] = mag_est; // max 22144, fs 16384
    }
}

/// True Magnitude for CU8 (sqrt can SIMD but float is slow).
void magnitude_true_cu8(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len)
{
    unsigned long i;
    for (i = 0; i < len; i++) {
        int16_t x = iq_buf[2 * i] - 128;
        int16_t y = iq_buf[2 * i + 1] - 128;
        y_buf[i]  = sqrt(x * x + y * y) * 128.0; // max 181, scaled 23170, fs 16384
    }
}

/// 122/128, 51/128 Magnitude Estimator for CS16 (SIMD has min/max).
void magnitude_est_cs16(int16_t const *iq_buf, uint16_t *y_buf, uint32_t len)
{
    unsigned long i;
    for (i = 0; i < len; i++) {
        uint32_t x = abs(iq_buf[2 * i]);
        uint32_t y = abs(iq_buf[2 * i + 1]);
        uint32_t mi = x < y ? x : y;
        uint32_t mx = x > y ? x : y;
        uint32_t mag_est = 122 * mx + 51 * mi;
        y_buf[i] = mag_est >> 8; // max 5668864, scaled 22144, fs 16384
    }
}

/// True Magnitude for CS16 (sqrt can SIMD but float is slow).
void magnitude_true_cs16(int16_t const *iq_buf, uint16_t *y_buf, uint32_t len)
{
    unsigned long i;
    for (i = 0; i < len; i++) {
        int32_t x = iq_buf[2 * i];
        int32_t y = iq_buf[2 * i + 1];
        y_buf[i]  = (int)sqrt(x * x + y * y) >> 1; // max 46341, scaled 23170, fs 16384
    }
}


// Fixed-point arithmetic on Q0.15
#define F_SCALE 15
#define S_CONST (1 << F_SCALE)
#define FIX(x) ((int)(x * S_CONST))

/** Something that might look like a IIR lowpass filter.

    [b,a] = butter(1, Wc) # low pass filter with cutoff pi*Wc radians
    Q1.15*Q15.0 = Q16.15
    Q16.15>>1 = Q15.14
    Q15.14 + Q15.14 + Q15.14 could possibly overflow to 17.14
    but the b coeffs are small so it wont happen
    Q15.14>>14 = Q15.0 \o/
*/
void baseband_low_pass_filter(uint16_t const *x_buf, int16_t *y_buf, uint32_t len, filter_state_t *state)
{
    ///  [b,a] = butter(1, 0.01) -> 3x tau (95%) ~100 samples
    //static int const a[FILTER_ORDER + 1] = {FIX(1.00000), FIX(0.96907)};
    //static int const b[FILTER_ORDER + 1] = {FIX(0.015466), FIX(0.015466)};
    ///  [b,a] = butter(1, 0.05) -> 3x tau (95%) ~20 samples
    static int const a[FILTER_ORDER + 1] = {FIX(1.00000), FIX(0.85408)};
    static int const b[FILTER_ORDER + 1] = {FIX(0.07296), FIX(0.07296)};

    unsigned long i;
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


/** Integer implementation of atan2() with int16_t normalized output.

    Returns arc tangent of y/x across all quadrants in radians.
    Error max 0.07 radians.
    Reference: http://dspguru.com/dsp/tricks/fixed-point-atan2-with-self-normalization
    @param y: Numerator (imaginary value of complex vector)
    @param x: Denominator (real value of complex vector)
    @return angle in radians (Pi equals INT16_MAX)
*/
int16_t atan2_int16(int16_t y, int16_t x)
{
    static int32_t const I_PI_4 = INT16_MAX/4;      // M_PI/4
    static int32_t const I_3_PI_4 = 3*INT16_MAX/4;  // 3*M_PI/4

    int32_t const abs_y = abs(y);
    int32_t angle;

    if (x >= 0) {    // Quadrant I and IV
        int32_t denom = (abs_y + x);
        if (denom == 0) denom = 1;  // Prevent divide by zero
        angle = I_PI_4 - I_PI_4 * (x - abs_y) / denom;
    } else {        // Quadrant II and III
        int32_t denom = (abs_y - x);
        if (denom == 0) denom = 1;  // Prevent divide by zero
        angle = I_3_PI_4 - I_PI_4 * (x + abs_y) / denom;
    }
    if (y < 0) angle = -angle;    // Negate if in III or IV
    return angle;
}

void baseband_demod_FM(uint8_t const *x_buf, int16_t *y_buf, unsigned long num_samples, demodfm_state_t *state)
{
    ///  [b,a] = butter(1, 0.1) -> 3x tau (95%) ~10 samples
    //static int const alp[2] = {FIX(1.00000), FIX(0.72654)};
    //static int const blp[2] = {FIX(0.13673), FIX(0.13673)};
    ///  [b,a] = butter(1, 0.2) -> 3x tau (95%) ~5 samples
    static int const alp[2] = {FIX(1.00000), FIX(0.50953)};
    static int const blp[2] = {FIX(0.24524), FIX(0.24524)};

    int16_t ar, ai;  // New IQ sample: x[n]
    int16_t br, bi;  // Old IQ sample: x[n-1]
    int32_t pr, pi;  // Phase difference vector
    int16_t xlp, ylp, xlp_old, ylp_old;  // Low Pass filter variables

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
        pr = ar*br+ai*bi;  // May exactly overflow an int16_t (-128*-128 + -128*-128)
        pi = ai*br-ar*bi;
//        xlp = (int16_t)((atan2f(pi, pr) / M_PI) * INT16_MAX);    // Floating point implementation
        xlp = atan2_int16(pi, pr);  // Integer implementation
//        xlp = pi;                    // Cheat and use only imaginary part (works OK, but is amplitude sensitive)
        // Low pass filter
        ylp = ((alp[1] * ylp_old >> 1) + (blp[0] * xlp >> 1) + (blp[1] * xlp_old >> 1)) >> (F_SCALE - 1);
        ylp_old = ylp; xlp_old = xlp;
        y_buf[n] = ylp;
    }

    // Store newest sample for next run
    state->br = ar; state->bi = ai;
    state->xlp = xlp_old; state->ylp = ylp_old;
}


// Fixed-point arithmetic on Q0.31 (actually Q0.30 to counter 64 signed trouble)
#define F_SCALE32 30
#define S_CONST32 (1 << F_SCALE32)
#define FIX32(x) ((int)(x * S_CONST32))

/// for evaluation.
int32_t atan2_int32(int32_t y, int32_t x)
{
    static int64_t const I_PI_4 = INT32_MAX / 4;          // M_PI/4
    static int64_t const I_3_PI_4 = 3ll * INT32_MAX / 4;  // 3*M_PI/4

    int64_t const abs_y = abs(y);
    int64_t angle;

    if (x >= 0) { // Quadrant I and IV
        int64_t denom = (abs_y + x);
        if (denom == 0) denom = 1; // Prevent divide by zero
        angle = I_PI_4 - I_PI_4 * (x - abs_y) / denom;
    } else { // Quadrant II and III
        int64_t denom = (abs_y - x);
        if (denom == 0) denom = 1; // Prevent divide by zero
        angle = I_3_PI_4 - I_PI_4 * (x + abs_y) / denom;
    }
    if (y < 0) angle = -angle; // Negate if in III or IV
    return angle;
}

/// for evaluation.
void baseband_demod_FM_cs16(int16_t const *x_buf, int16_t *y_buf, unsigned long num_samples, demodfm_state_t *state)
{
    ///  [b,a] = butter(1, 0.1) -> 3x tau (95%) ~10 samples
    //static int const alp[2] = {FIX32(1.00000), FIX32(0.72654)};
    //static int const blp[2] = {FIX32(0.13673), FIX32(0.13673)};
    ///  [b,a] = butter(1, 0.2) -> 3x tau (95%) ~5 samples
    static int64_t const alp[2] = {FIX32(1.00000), FIX32(0.50953)};
    static int64_t const blp[2] = {FIX32(0.24524), FIX32(0.24524)};

    int32_t ar, ai;  // New IQ sample: x[n]
    int32_t br, bi;  // Old IQ sample: x[n-1]
    int64_t pr, pi;  // Phase difference vector
    int32_t xlp, ylp, xlp_old, ylp_old;  // Low Pass filter variables

    // Pre-feed old sample
    ar = state->br; ai = state->bi;
    xlp_old = state->xlp; ylp_old = state->ylp;

    for (unsigned n = 0; n < num_samples; n++) {
        // delay old sample
        br = ar;
        bi = ai;
        // get new sample
        ar = x_buf[2*n];
        ai = x_buf[2*n+1];
        // Calculate phase difference vector: x[n] * conj(x[n-1])
        pr = (int64_t)ar*br+ai*bi;  // May exactly overflow an int32_t (-32768*-32768 + -32768*-32768)
        pi = (int64_t)ai*br-ar*bi;
//        xlp = (int32_t)((atan2f(pi, pr) / M_PI) * INT32_MAX);    // Floating point implementation
        xlp = atan2_int32(pi, pr);  // Integer implementation
//        xlp = atan2_int16(pi>>16, pr>>16) << 16;  // Integer implementation
//        xlp = pi;                    // Cheat and use only imaginary part (works OK, but is amplitude sensitive)
        // Low pass filter
        ylp = (alp[1] * ylp_old + blp[0] * xlp + blp[1] * xlp_old) >> F_SCALE32;
        ylp_old = ylp; xlp_old = xlp;
        y_buf[n] = ylp >> 16; // not really losing info here, maybe optimize earlier
    }

    // Store newest sample for next run
    state->br = ar; state->bi = ai;
    state->xlp = xlp_old; state->ylp = ylp_old;
}

void baseband_init(void)
{
    calc_squares();
}
