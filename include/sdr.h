/** @file
    SDR input from RTLSDR or SoapySDR.

    Copyright (C) 2018 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_SDR_H_
#define INCLUDE_SDR_H_

#include <stdint.h>

typedef struct sdr_dev sdr_dev_t;
typedef void (*sdr_read_cb_t)(unsigned char *buf, uint32_t len, void *ctx);

/** Find the closest matching device, optionally report status.

    @param out_dev device output returned
    @param sample_size stream output sample width returned
    @param dev_query a string to be parsed as device spec
    @param verbose the verbosity level for reports to stderr
    @return dev 0 if successful
*/
int sdr_open(sdr_dev_t **out_dev, int *sample_size, char *dev_query, int verbose);

/** Close the device, optionally report status.

    @param dev the device handle
    @param verbose the verbosity level for reports to stderr
    @return 0 on success
*/
int sdr_close(sdr_dev_t *dev);

/** Set device frequency, optionally report status.

    @param dev the device handle
    @param frequency in Hz
    @param verbose the verbosity level for reports to stderr
    @return 0 on success
*/
int sdr_set_center_freq(sdr_dev_t *dev, uint32_t freq, int verbose);

/** Get device frequency.

    @param dev the device handle
    @return frequency in Hz on success, 0 otherwise
*/
uint32_t sdr_get_center_freq(sdr_dev_t *dev);

/** Set the frequency correction value for the device, optionally report status.

    @param dev the device handle
    @param ppm_error correction value in parts per million (ppm)
    @param verbose the verbosity level for reports to stderr
    @return 0 on success
*/
int sdr_set_freq_correction(sdr_dev_t *dev, int ppm, int verbose);

/** Enable auto gain, optionally report status.

    @param dev the device handle
    @param verbose the verbosity level for reports to stderr
    @return 0 on success
*/
int sdr_set_auto_gain(sdr_dev_t *dev, int verbose);

/** Set tuner gain or gain elements, optionally report status.

    @param dev the device handle
    @param gain_str in tenths of a dB for RTL-SDR, string of gain element pairs (example LNA=40,VGA=20,AMP=0), or string of overall gain, in dB
    @param verbose the verbosity level for reports to stderr
    @return 0 on success
*/
int sdr_set_tuner_gain(sdr_dev_t *dev, char *gain_str, int verbose);

/** Set device sample rate, optionally report status.

    @param dev the device handle
    @param samp_rate in samples/second
    @param verbose the verbosity level for reports to stderr
    @return 0 on success
*/
int sdr_set_sample_rate(sdr_dev_t *dev, uint32_t rate, int verbose);

/** Set device antenna.

    @param dev the device handle
    @param antenna_str name of the antenna (example 'Tuner 2 50 ohm')
    @param verbose the verbosity level for reports to stderr
    @return 0 on success
*/
int sdr_set_antenna(sdr_dev_t *dev, char *antenna_str, int verbose);

/** Get device sample rate.

    @param dev the device handle
    @return sample rate in samples/second on success, 0 otherwise
*/
uint32_t sdr_get_sample_rate(sdr_dev_t *dev);

/** Apply a list of sdr settings.

    @param dev the device handle
    @param sdr_settings keyword list of settings
    @param verbose the verbosity level for reports to stderr
    @return 0 on success
*/
int sdr_apply_settings(sdr_dev_t *dev, char const *sdr_settings, int verbose);

/** Activate stream (only needed for SoapySDR).

    @param dev the device handle
    @return 0 on success
*/
int sdr_activate(sdr_dev_t *dev);

/** Deactivate stream (only needed for SoapySDR).

    @param dev the device handle
    @return 0 on success
*/
int sdr_deactivate(sdr_dev_t *dev);

/** Reset buffer (only needed for RTL-SDR), optionally report status.

    @param dev the device handle
    @param verbose the verbosity level for reports to stderr
    @return 0 on success
*/
int sdr_reset(sdr_dev_t *dev, int verbose);

int sdr_start(sdr_dev_t *dev, sdr_read_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len);
int sdr_stop(sdr_dev_t *dev);

#endif /* INCLUDE_SDR_H_ */
