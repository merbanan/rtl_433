/*
 * rtl-sdr, a poor man's SDR using a Realtek RTL2832 based DVB-stick
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 *(at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __RTL_SDR_H
#define __RTL_SDR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "rtl-sdr_export.h"

typedef struct rtlsdr_dev rtlsdr_dev_t;

uint32_t RTLSDR_API rtlsdr_get_device_count(void);

RTLSDR_API const char*  rtlsdr_get_device_name(uint32_t index);

RTLSDR_API rtlsdr_dev_t*  rtlsdr_open(uint32_t index);

int RTLSDR_API rtlsdr_close(rtlsdr_dev_t *dev);

/* configuration functions */

int RTLSDR_API rtlsdr_set_center_freq(rtlsdr_dev_t *dev, uint32_t freq);

int RTLSDR_API rtlsdr_get_center_freq(rtlsdr_dev_t *dev);

int RTLSDR_API rtlsdr_set_freq_correction(rtlsdr_dev_t *dev, int ppm);

int RTLSDR_API rtlsdr_get_freq_correction(rtlsdr_dev_t *dev);

int RTLSDR_API rtlsdr_set_tuner_gain(rtlsdr_dev_t *dev, int gain);

int RTLSDR_API rtlsdr_get_tuner_gain(rtlsdr_dev_t *dev);

/* this will select the baseband filters according to the requested sample rate */
int RTLSDR_API rtlsdr_set_sample_rate(rtlsdr_dev_t *dev, uint32_t rate);

int RTLSDR_API rtlsdr_get_sample_rate(rtlsdr_dev_t *dev);

/* streaming functions */

int RTLSDR_API rtlsdr_reset_buffer(rtlsdr_dev_t *dev);

int RTLSDR_API rtlsdr_read_sync(rtlsdr_dev_t *dev, void *buf, int len, int *n_read);

#ifdef __cplusplus
}
#endif

#endif /* __RTL_SDR_H */
