/** @file
    Various functions for baseband sample processing.

    Copyright (C) 2012 by Benjamin Larsson <benjamin@southpole.se>
    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_BASEBAND_H_
#define INCLUDE_BASEBAND_H_

#include <stdint.h>
#include <math.h>

/** This will give a noisy envelope of OOK/ASK signals.

    Subtract the bias (-128) and get an envelope estimation (absolute squared).
    @param iq_buf input samples (I/Q samples in interleaved uint8)
    @param[out] y_buf output buffer
    @param len number of samples to process
    @return the average level in dB
*/
float envelope_detect(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len);

// for evaluation
float envelope_detect_nolut(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len);
float magnitude_est_cu8(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len);
float magnitude_true_cu8(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len);
float magnitude_est_cs16(int16_t const *iq_buf, uint16_t *y_buf, uint32_t len);
float magnitude_true_cs16(int16_t const *iq_buf, uint16_t *y_buf, uint32_t len);

#define AMP_TO_DB(x) (10.0f * ((x) > 0 ? log10f(x) : 0) - 42.1442f)  // 10*log10f(16384.0f)
#define MAG_TO_DB(x) (20.0f * ((x) > 0 ? log10f(x) : 0) - 84.2884f)  // 20*log10f(16384.0f)
#ifdef __exp10f
#define _exp10f(x) __exp10f(x)
#else
#define _exp10f(x) powf(10, x)
#endif
#define DB_TO_AMP(x) ((int)(_exp10f(((x) + 42.1442f) / 10.0f)))  // 10*log10f(16384.0f)
#define DB_TO_MAG(x) ((int)(_exp10f(((x) + 84.2884f) / 20.0f)))  // 20*log10f(16384.0f)
#define DB_TO_AMP_F(x) ((int)(0.5 + _exp10f((x) / 10.0f)))
#define DB_TO_MAG_F(x) ((int)(0.5 + _exp10f((x) / 20.0f)))

/*
tabulated magnitude and amplitude values:
10^(( 3 + 84.2884) / 20) = 23143        10^(( 3 + 42.1442) / 10) = 32690
10^(( 2 + 84.2884) / 20) = 20626        10^(( 2 + 42.1442) / 10) = 25967
10^(( 1 + 84.2884) / 20) = 18383        10^(( 1 + 42.1442) / 10) = 20626
10^(( 0 + 84.2884) / 20) = 16384        10^(( 0 + 42.1442) / 10) = 16384
10^((-1 + 84.2884) / 20) = 14602        10^((-1 + 42.1442) / 10) = 13014
10^((-2 + 84.2884) / 20) = 13014        10^((-2 + 42.1442) / 10) = 10338
10^((-3 + 84.2884) / 20) = 11599        10^((-3 + 42.1442) / 10) =  8211
10^((-4 + 84.2884) / 20) = 10338        10^((-4 + 42.1442) / 10) =  6523
10^((-5 + 84.2884) / 20) =  9213        10^((-5 + 42.1442) / 10) =  5181
10^((-6 + 84.2884) / 20) =  8211        10^((-6 + 42.1442) / 10) =  4115
10^((-7 + 84.2884) / 20) =  7318        10^((-7 + 42.1442) / 10) =  3269
10^((-8 + 84.2884) / 20) =  6523        10^((-8 + 42.1442) / 10) =  2597
10^((-9 + 84.2884) / 20) =  5813        10^((-9 + 42.1442) / 10) =  2063
10^((-10 + 84.2884) / 20) = 5181        10^((-10 + 42.1442) / 10) = 1638
10^((-11 + 84.2884) / 20) = 4618        10^((-11 + 42.1442) / 10) = 1301
10^((-12 + 84.2884) / 20) = 4115        10^((-12 + 42.1442) / 10) = 1034
10^((-13 + 84.2884) / 20) = 3668        10^((-13 + 42.1442) / 10) =  821
10^((-14 + 84.2884) / 20) = 3269        10^((-14 + 42.1442) / 10) =  652
10^((-15 + 84.2884) / 20) = 2914        10^((-15 + 42.1442) / 10) =  518
10^((-16 + 84.2884) / 20) = 2597        10^((-16 + 42.1442) / 10) =  412
10^((-17 + 84.2884) / 20) = 2314        10^((-17 + 42.1442) / 10) =  327
10^((-18 + 84.2884) / 20) = 2063        10^((-18 + 42.1442) / 10) =  260
10^((-19 + 84.2884) / 20) = 1838        10^((-19 + 42.1442) / 10) =  206
10^((-20 + 84.2884) / 20) = 1638        10^((-20 + 42.1442) / 10) =  164
10^((-21 + 84.2884) / 20) = 1460        10^((-21 + 42.1442) / 10) =  130
10^((-22 + 84.2884) / 20) = 1301        10^((-22 + 42.1442) / 10) =  103
10^((-23 + 84.2884) / 20) = 1160        10^((-23 + 42.1442) / 10) =   82
10^((-24 + 84.2884) / 20) = 1034        10^((-24 + 42.1442) / 10) =   65
10^((-25 + 84.2884) / 20) =  921        10^((-25 + 42.1442) / 10) =   52
10^((-26 + 84.2884) / 20) =  821        10^((-26 + 42.1442) / 10) =   41
10^((-27 + 84.2884) / 20) =  732        10^((-27 + 42.1442) / 10) =   33
10^((-28 + 84.2884) / 20) =  652        10^((-28 + 42.1442) / 10) =   26
10^((-29 + 84.2884) / 20) =  581        10^((-29 + 42.1442) / 10) =   21
10^((-30 + 84.2884) / 20) =  518        10^((-30 + 42.1442) / 10) =   16
10^((-31 + 84.2884) / 20) =  462        10^((-31 + 42.1442) / 10) =   13
10^((-32 + 84.2884) / 20) =  412        10^((-32 + 42.1442) / 10) =   10
*/

#define FILTER_ORDER 1

/// Filter state buffer.
typedef struct filter_state {
    int16_t y[FILTER_ORDER];
    int16_t x[FILTER_ORDER];
} filter_state_t;

/// FM_Demod state buffer.
typedef struct demodfm_state {
    int32_t xr;        ///< Last I/Q sample, real part
    int32_t xi;        ///< Last I/Q sample, imag part
    int32_t xf;        ///< Last Instantaneous frequency
    int32_t yf;        ///< Last Instantaneous frequency, low pass filtered
    uint32_t rate;     ///< Current sample rate
    int32_t alp_16[2]; ///< Current low pass filter A coeffs, 16 bit
    int32_t blp_16[2]; ///< Current low pass filter B coeffs, 16 bit
    int64_t alp_32[2]; ///< Current low pass filter A coeffs, 32 bit
    int64_t blp_32[2]; ///< Current low pass filter B coeffs, 32 bit
} demodfm_state_t;

/** Lowpass filter.

    Function is stateful.
    @param x_buf input samples to be filtered
    @param[out] y_buf output from filter
    @param len number of samples to process
    @param[in,out] state State to store between chunk processing
*/
void baseband_low_pass_filter(uint16_t const *x_buf, int16_t *y_buf, uint32_t len, filter_state_t *state);

/** FM demodulator.

    Function is stateful.
    @param x_buf input samples (I/Q samples in interleaved uint8)
    @param[out] y_buf output from FM demodulator
    @param num_samples number of samples to process
    @param low_pass Low-pass filter frequency or ratio
    @param[in,out] state State to store between chunk processing
*/
void baseband_demod_FM(uint8_t const *x_buf, int16_t *y_buf, unsigned long num_samples, uint32_t samp_rate, float low_pass, demodfm_state_t *state);

/// For evaluation.
void baseband_demod_FM_cs16(int16_t const *x_buf, int16_t *y_buf, unsigned long num_samples, uint32_t samp_rate, float low_pass, demodfm_state_t *state);

/** Initialize tables and constants.
    Should be called once at startup.
*/
void baseband_init(void);

#endif /* INCLUDE_BASEBAND_H_ */
