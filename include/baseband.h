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
*/
void envelope_detect(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len);

// for evaluation
void envelope_detect_nolut(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len);
void magnitude_est_cu8(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len);
void magnitude_true_cu8(uint8_t const *iq_buf, uint16_t *y_buf, uint32_t len);
void magnitude_est_cs16(int16_t const *iq_buf, uint16_t *y_buf, uint32_t len);
void magnitude_true_cs16(int16_t const *iq_buf, uint16_t *y_buf, uint32_t len);

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

#define FILTER_ORDER 1

/// Filter state buffer.
typedef struct filter_state {
    int16_t y[FILTER_ORDER];
    int16_t x[FILTER_ORDER];
} filter_state_t;

/// FM_Demod state buffer.
typedef struct demodfm_state {
    int32_t br, bi;   // Last I/Q sample
    int32_t xlp, ylp; // Low-pass filter state
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
    @param[in,out] state State to store between chunk processing
    @param fpdm Index of filter setting to use
*/
void baseband_demod_FM(uint8_t const *x_buf, int16_t *y_buf, unsigned long num_samples, demodfm_state_t *state, unsigned fpdm);

/// For evaluation.
void baseband_demod_FM_cs16(int16_t const *x_buf, int16_t *y_buf, unsigned long num_samples, demodfm_state_t *state, unsigned fpdm);

/** Initialize tables and constants.
    Should be called once at startup.
*/
void baseband_init(void);

#endif /* INCLUDE_BASEBAND_H_ */
