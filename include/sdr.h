/**
 * SDR input from RTLSDR or SoapySDR
 *
 * Copyright (C) 2018 Christian Zuckschwerdt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef INCLUDE_SDR_H_
#define INCLUDE_SDR_H_

#include <stdint.h>

typedef struct sdr_dev sdr_dev_t;
typedef void (*sdr_read_cb_t)(unsigned char *buf, uint32_t len, void *ctx);

int sdr_open(sdr_dev_t **out_dev, int *sample_size, char const *dev_query, int verbose);
int sdr_close(sdr_dev_t *dev);

int sdr_set_center_freq(sdr_dev_t *dev, uint32_t freq, int verbose);
uint32_t sdr_get_center_freq(sdr_dev_t *dev);

int sdr_set_freq_correction(sdr_dev_t *dev, int ppm, int verbose);

int sdr_set_auto_gain(sdr_dev_t *dev, int verbose);
int sdr_set_tuner_gain(sdr_dev_t *dev, int gain, int verbose);

int sdr_set_sample_rate(sdr_dev_t *dev, uint32_t rate, int verbose);
uint32_t sdr_get_sample_rate(sdr_dev_t *dev);

int sdr_reset(sdr_dev_t *dev);
int sdr_start(sdr_dev_t *dev, sdr_read_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len);
int sdr_stop(sdr_dev_t *dev);

#endif /* INCLUDE_SDR_H_ */
