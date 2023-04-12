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

#define SDR_DEFAULT_BUF_NUMBER 15
#define SDR_DEFAULT_BUF_LENGTH 0x40000

typedef struct sdr_dev sdr_dev_t;

typedef enum sdr_event_flags {
    SDR_EV_EMPTY = 0,
    SDR_EV_DATA = 1 << 0,
    SDR_EV_RATE = 1 << 1,
    SDR_EV_CORR = 1 << 2,
    SDR_EV_FREQ = 1 << 3,
    SDR_EV_GAIN = 1 << 4,
} sdr_event_flags_t;

typedef struct sdr_event {
    sdr_event_flags_t ev;
    uint32_t sample_rate;
    int freq_correction;
    uint32_t center_frequency;
    char const *gain_str;
    void *buf;
    int len;
} sdr_event_t;

typedef void (*sdr_event_cb_t)(sdr_event_t *ev, void *ctx);

/** Find the closest matching device, optionally report status.

    @param out_dev device output returned
    @param dev_query a string to be parsed as device spec
    @param verbose the verbosity level for reports to stderr
    @return dev 0 if successful
*/
int sdr_open(sdr_dev_t **out_dev, char const *dev_query, int verbose);

/** Close the device.

    @note
    All previous sdr_event_t buffers will be invalid after calling sdr_close().
    Make sure none are in use anymore.

    @param dev the device handle
    @return 0 on success
*/
int sdr_close(sdr_dev_t *dev);

/** Get device info.

    @param dev the device handle
    @return JSON device info string
*/
char const *sdr_get_dev_info(sdr_dev_t *dev);

/** Get sample size.

    @param dev the device handle
    @return Sample size of I/Q elements in bytes (CU8: 2, CS16: 4, ...)
*/
int sdr_get_sample_size(sdr_dev_t *dev);

/** Get sample signedness.

    @param dev the device handle
    @return 1 if the samples are signed (CS8, CS16, ...), 0 otherwise (CU8, ...)
*/
int sdr_get_sample_signed(sdr_dev_t *dev);

/** Set device frequency, optionally report status.

    @param dev the device handle
    @param freq in Hz
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
    @param ppm correction value in parts per million (ppm)
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
int sdr_set_tuner_gain(sdr_dev_t *dev, char const *gain_str, int verbose);

/** Set device sample rate, optionally report status.

    @param dev the device handle
    @param rate in samples/second
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
int sdr_set_antenna(sdr_dev_t *dev, char const *antenna_str, int verbose);

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

/** Start the SDR data acquisition.

    @note
    All previous sdr_event_t buffers will be invalid if @p buf_num or @p buf_len changed.
    Make sure none are in use anymore.

    @param dev the device handle
    @param async_cb a callback for sdr_event_t messages
    @param async_ctx a user context to be passed to @p async_cb
    @param buf_num the number of buffers to keep
    @param buf_len the size in bytes of each buffer
    @return 0 on success
*/
int sdr_start(sdr_dev_t *dev, sdr_event_cb_t async_cb, void *async_ctx, uint32_t buf_num, uint32_t buf_len);
int sdr_start_sync(sdr_dev_t *dev, sdr_event_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len);

/** Stop the SDR data acquisition.

    @note
    All previous sdr_event_t buffers will remain valid until sdr_close().

    @param dev the device handle
    @return 0 on success
*/
int sdr_stop(sdr_dev_t *dev);
int sdr_stop_sync(sdr_dev_t *dev);

/** Redirect SoapySDR library logging.
*/
void sdr_redirect_logging(void);

#endif /* INCLUDE_SDR_H_ */
