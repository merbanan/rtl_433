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

#include "logger.h"
#include "r_util.h"

static uint16_t scaled_squares[256];

/// precalculate lookup table for envelope detection.
static void calc_squares(void)
{
    if (scaled_squares[0])
        return; // already initialized
    int i;
    for (i = 0; i < 256; i++)
        scaled_squares[i] = (127 - i) * (127 - i);
}

// This will give a noisy envelope of OOK/ASK signals.
// Subtract the bias (-128) and get an envelope estimation.
float envelope_detect(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len)
{
    unsigned long i;
    uint32_t sum = 0;
    for (i = 0; i < len; i++) {
        y_buf[i] = scaled_squares[iq_buf[2 * i ]] + scaled_squares[iq_buf[2 * i + 1]];
        sum += y_buf[i];
    }
    return len > 0 && sum >= len ? AMP_TO_DB((float)sum / len) : AMP_TO_DB(1);
}

/// This will give a noisy envelope of OOK/ASK signals.
/// Subtracts the bias (-128) and calculates the norm (scaled by 16384).
/// Using a LUT is slower for O1 and above.
float envelope_detect_nolut(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len)
{
    unsigned long i;
    uint32_t sum = 0;
    for (i = 0; i < len; i++) {
        int16_t x = 127 - iq_buf[2 * i];
        int16_t y = 127 - iq_buf[2 * i + 1];
        y_buf[i]  = x * x + y * y; // max 32768, fs 16384
        sum += y_buf[i];
    }
    return len > 0 && sum >= len ? AMP_TO_DB((float)sum / len) : AMP_TO_DB(1);
}

/// 122/128, 51/128 Magnitude Estimator for CU8 (SIMD has min/max).
/// Note that magnitude emphasizes quiet signals / deemphasizes loud signals.
float magnitude_est_cu8(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len)
{
    unsigned long i;
    uint32_t sum = 0;
    for (i = 0; i < len; i++) {
        uint16_t x = abs(iq_buf[2 * i] - 128);
        uint16_t y = abs(iq_buf[2 * i + 1] - 128);
        uint16_t mi = x < y ? x : y;
        uint16_t mx = x > y ? x : y;
        uint16_t mag_est = 122 * mx + 51 * mi;
        y_buf[i] = mag_est; // max 22144, fs 16384
        sum += y_buf[i];
    }
    return len > 0 && sum >= len ? MAG_TO_DB((float)sum / len) : MAG_TO_DB(1);
}

/// True Magnitude for CU8 (sqrt can SIMD but float is slow).
float magnitude_true_cu8(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len)
{
    unsigned long i;
    uint32_t sum = 0;
    for (i = 0; i < len; i++) {
        int16_t x = iq_buf[2 * i] - 128;
        int16_t y = iq_buf[2 * i + 1] - 128;
        y_buf[i]  = (uint16_t)(sqrt(x * x + y * y) * 128.0); // max 181, scaled 23170, fs 16384
        sum += y_buf[i];
    }
    return len > 0 && sum >= len ? MAG_TO_DB((float)sum / len) : MAG_TO_DB(1);
}

/// 122/128, 51/128 Magnitude Estimator for CS16 (SIMD has min/max).
float magnitude_est_cs16(int16_t const *iq_buf, uint16_t *y_buf, uint32_t len)
{
    unsigned long i;
    uint32_t sum = 0;
    for (i = 0; i < len; i++) {
        uint32_t x = abs(iq_buf[2 * i]);
        uint32_t y = abs(iq_buf[2 * i + 1]);
        uint32_t mi = x < y ? x : y;
        uint32_t mx = x > y ? x : y;
        uint32_t mag_est = 122 * mx + 51 * mi;
        y_buf[i] = mag_est >> 8; // max 5668864, scaled 22144, fs 16384
        sum += y_buf[i];
    }
    return len > 0 && sum >= len ? MAG_TO_DB((float)sum / len) : MAG_TO_DB(1);
}

/// True Magnitude for CS16 (sqrt can SIMD but float is slow).
float magnitude_true_cs16(int16_t const *iq_buf, uint16_t *y_buf, uint32_t len)
{
    unsigned long i;
    uint32_t sum = 0;
    for (i = 0; i < len; i++) {
        int32_t x = iq_buf[2 * i];
        int32_t y = iq_buf[2 * i + 1];
        y_buf[i]  = (int)sqrt(x * x + y * y) >> 1; // max 46341, scaled 23170, fs 16384
        sum += y_buf[i];
    }
    return len > 0 && sum >= len ? MAG_TO_DB((float)sum / len) : MAG_TO_DB(1);
}


// Fixed-point arithmetic on Q0.15
#define F_SCALE 15
#define S_CONST (1 << F_SCALE)
#define FIX(x) ((int)(x * S_CONST))

/** Something that might look like a IIR lowpass filter.

    [b,a] = butter(1, Wc) # low pass filter with cutoff pi*Wc radians
    - Q1.15*Q15.0 = Q16.15
    - Q16.15>>1 = Q15.14
    - Q15.14 + Q15.14 + Q15.14 could possibly overflow to 17.14
    - but the b coeffs are small so it won't happen
    - Q15.14>>14 = Q15.0
*/
void baseband_low_pass_filter(uint16_t const *x_buf, int16_t *y_buf, uint32_t len, filter_state_t *state)
{
    ///  [b,a] = butter(1, 0.01) -> 3x tau (95%) ~100 samples
    //static int const a[FILTER_ORDER + 1] = {FIX(1.00000) >> 1, FIX(0.96907) >> 1};
    //static int const b[FILTER_ORDER + 1] = {FIX(0.015466) >> 1, FIX(0.015466) >> 1};
    ///  [b,a] = butter(1, 0.05) -> 3x tau (95%) ~20 samples
    static int const a[FILTER_ORDER + 1] = {FIX(1.00000) >> 1, FIX(0.85408) >> 1};
    static int const b[FILTER_ORDER + 1] = {FIX(0.07296) >> 1, FIX(0.07296) >> 1};
    // note that coeffs are prescaled by div 2

    // Prevent out of bounds access
    if (len < FILTER_ORDER) {
        return;
    }

    // Calculate first sample
    y_buf[0] = (a[1] * state->y[0] + b[0] * (x_buf[0] + state->x[0])) >> (F_SCALE - 1); // note: prescaled, b[0]==b[1]
    for (unsigned long i = 1; i < len; i++) {
        y_buf[i] = (a[1] * y_buf[i - 1] + b[0] * (x_buf[i] + x_buf[i - 1])) >> (F_SCALE - 1); // note: prescaled, b[0]==b[1]
    }

    // Save last samples
    memcpy(state->x, &x_buf[len - FILTER_ORDER], FILTER_ORDER * sizeof (int16_t));
    memcpy(state->y, &y_buf[len - FILTER_ORDER], FILTER_ORDER * sizeof (int16_t));
}


/** Integer implementation of atan2() with int16_t normalized output.

    Returns arc tangent of y/x across all quadrants in radians.
    Error max 0.07 radians.
    Reference: http://dspguru.com/dsp/tricks/fixed-point-atan2-with-self-normalization
    @param y Numerator (imaginary value of complex vector)
    @param x Denominator (real value of complex vector)
    @return angle in radians (Pi equals INT16_MAX)
*/
static int16_t atan2_int16(int32_t y, int32_t x)
{
    static int32_t const I_PI_4 = INT16_MAX/4;      // M_PI/4
    static int32_t const I_3_PI_4 = 3*INT16_MAX/4;  // 3*M_PI/4

    int32_t const abs_y = abs(y);
    int32_t angle;

    if (!x && !y) return 0; // We would get 8191 with the code below

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

/// Fast Instantaneous frequency and Low Pass filter, CU8 samples
void baseband_demod_FM(uint8_t const *x_buf, int16_t *y_buf, unsigned long num_samples, uint32_t samp_rate, float low_pass, demodfm_state_t *state)
{
    // Select filter coeffs, [b,a] = butter(1, cutoff)
    // e.g [b,a] = butter(1, 0.1) -> 3x tau (95%) ~10 samples, 250k -> 40us, 1024k -> 10us
    // a = 1.00000, 0.72654; b = 0.13673, 0.13673;
    // e.g. [b,a] = butter(1, 0.2) -> 3x tau (95%) ~5 samples, 250k -> 20us, 1024k -> 5us
    // a = 1.00000, 0.50953; b = 0.24524, 0.24524;
    if (state->rate != samp_rate) {
        if (low_pass > 1e4f) {
            low_pass = low_pass / samp_rate;
        } else if (low_pass >= 1.0f) {
            low_pass = 1e6f / low_pass / samp_rate;
        }
        print_logf(LOG_NOTICE, "Baseband", "low pass filter for %u Hz at cutoff %.0f Hz, %.1f us",
                samp_rate, samp_rate * low_pass, 1e6 / (samp_rate * low_pass));
        double ita  = 1.0 / tan(M_PI_2 * low_pass);
        double gain = 1.0 / (1.0 + ita) / 2; // prescaled by div 2
        state->alp_16[0] = FIX(1.0);
        state->alp_16[1] = FIX((ita - 1.0) * gain); // scaled by -1
        state->blp_16[0] = FIX(gain);
        state->blp_16[1] = FIX(gain);
        state->rate      = samp_rate;
    }
    int32_t const *alp = state->alp_16;
    int32_t const *blp = state->blp_16;

    // Pre-feed old sample
    int16_t x0r = state->xr; // IQ sample: x[n], real
    int16_t x0i = state->xi; // IQ sample: x[n], imag
    int16_t x0f = state->xf; // Instantaneous frequency
    int16_t y0f = state->yf; // Instantaneous frequency, low pass filtered

    for (unsigned n = 0; n < num_samples; n++) {
        int16_t x1r, x1i; // Old IQ sample: x[n-1]
        int16_t x1f, y1f; // Instantaneous frequency, old sample
        int32_t pr, pi;   // Phase difference vector

        // delay old sample
        x1r = x0r;
        x1i = x0i;
        y1f = y0f;
        x1f = x0f;
        // get new sample
        x0r = *x_buf++ - 128;
        x0i = *x_buf++ - 128;
        // Calculate phase difference vector: x[n] * conj(x[n-1])
        pr = x0r * x1r + x0i * x1i; // May exactly overflow an int16_t (-128*-128 + -128*-128)
        pi = x0i * x1r - x0r * x1i;
        // xlp = (int16_t)((atan2f(pi, pr) / M_PI) * INT16_MAX); // Floating point implementation
        x0f = atan2_int16(pi, pr); // Integer implementation
        // xlp = pi; // Cheat and use only imaginary part (works OK, but is amplitude sensitive)
        // Low pass filter
        // y0f      = ((alp[1] * y1f >> 1) + (blp[0] * x0f >> 1) + (blp[1] * x1f >> 1)) >> (F_SCALE - 1);
        y0f      = (alp[1] * y1f + blp[0] * (x0f + x1f)) >> (F_SCALE - 1); // note: prescaled, blp[0]==blp[1]
        *y_buf++ = y0f;
    }

    // Store newest sample for next run
    state->xr = x0r;
    state->xi = x0i;
    state->xf = x0f;
    state->yf = y0f;
}


// Fixed-point arithmetic on Q0.31 (actually Q0.30 to counter 64 signed trouble)
#define F_SCALE32 30
#define S_CONST32 (1 << F_SCALE32)
#define FIX32(x) ((int)(x * S_CONST32))

/// for evaluation.
static int32_t atan2_int32(int32_t y, int32_t x)
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

/// Fast Instantaneous frequency and Low Pass filter, CS16 samples.
void baseband_demod_FM_cs16(int16_t const *x_buf, int16_t *y_buf, unsigned long num_samples, uint32_t samp_rate, float low_pass, demodfm_state_t *state)
{
    // Select filter coeffs, [b,a] = butter(1, cutoff)
    // e.g [b,a] = butter(1, 0.1) -> 3x tau (95%) ~10 samples, 250k -> 40us, 1024k -> 10us
    // a = 1.00000, 0.72654; b = 0.13673, 0.13673;
    // e.g. [b,a] = butter(1, 0.2) -> 3x tau (95%) ~5 samples, 250k -> 20us, 1024k -> 5us
    // a = 1.00000, 0.50953; b = 0.24524, 0.24524;
    if (state->rate != samp_rate) {
        if (low_pass > 1e4f) {
            low_pass = low_pass / samp_rate;
        } else if (low_pass >= 1.0f) {
            low_pass = 1e6f / low_pass / samp_rate;
        }
        print_logf(LOG_NOTICE, "Baseband", "low pass filter for %u Hz at cutoff %.0f Hz, %.1f us",
                samp_rate, samp_rate * low_pass, 1e6 / (samp_rate * low_pass));
        double ita  = 1.0 / tan(M_PI_2 * low_pass);
        double gain = 1.0 / (1.0 + ita);
        state->alp_32[0] = FIX32(1.0);
        state->alp_32[1] = FIX32((ita - 1.0) * gain); // scaled by -1
        state->blp_32[0] = FIX32(gain);
        state->blp_32[1] = FIX32(gain);
        state->rate      = samp_rate;
    }
    int64_t const *alp = state->alp_32;
    int64_t const *blp = state->blp_32;

    // Pre-feed old sample
    int32_t x0r = state->xr; // IQ sample: x[n], real
    int32_t x0i = state->xi; // IQ sample: x[n], imag
    int32_t x0f = state->xf; // Instantaneous frequency
    int32_t y0f = state->yf; // Instantaneous frequency, low pass filtered

    for (unsigned n = 0; n < num_samples; n++) {
        int32_t x1r, x1i; // Old IQ sample: x[n-1]
        int32_t x1f, y1f; // Instantaneous frequency, old sample
        int64_t pr, pi;   // Phase difference vector

        // delay old sample
        x1r = x0r;
        x1i = x0i;
        y1f = y0f;
        x1f = x0f;
        // get new sample
        x0r = *x_buf++;
        x0i = *x_buf++;
        // Calculate phase difference vector: x[n] * conj(x[n-1])
        pr = (int64_t)x0r * x1r + (int64_t)x0i * x1i; // May exactly overflow an int32_t (-32768*-32768 + -32768*-32768)
        pi = (int64_t)x0i * x1r - (int64_t)x0r * x1i;
        // xlp = (int32_t)((atan2f(pi, pr) / M_PI) * INT32_MAX); // Floating point implementation
        x0f = atan2_int32(pi, pr); // Integer implementation
        // xlp = atan2_int16(pi >> 16, pr >> 16) << 16; // Integer implementation, truncated
        // xlp = pi; // Cheat and use only imaginary part (works OK, but is amplitude sensitive)
        // Low pass filter
        // y0f      = (alp[1] * y1f + blp[0] * x0f + blp[1] * x1f) >> F_SCALE32;
        y0f      = (alp[1] * y1f + blp[0] * ((int64_t)x0f + x1f)) >> F_SCALE32; // note: blp[0]==blp[1]
        *y_buf++ = y0f >> 16; // not really losing info here, maybe optimize earlier
    }

    // Store newest sample for next run
    state->xr = x0r;
    state->xi = x0i;
    state->xf = x0f;
    state->yf = y0f;
}

void baseband_init(void)
{
    calc_squares();
}
