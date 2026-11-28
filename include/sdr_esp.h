/** @file
    ESP-IDF input: an RTL2832U dongle on the USB host port of an ESP32-P4.

    Only built with ESP_RTL_SDR, i.e. on ESP-IDF with the esp_rtl_sdr driver component.
    esp_rtl_sdr is AGPL-3.0-only, so a firmware that links it is AGPL-3.0 as a whole.

    Copyright (C) 2026 David Coulson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_SDR_ESP_H_
#define INCLUDE_SDR_ESP_H_

#include <stddef.h>
#include <stdint.h>
#include "sdr.h"

/* For applications that embed rtl_433 */

/// USB driver tuning, applied when the driver is installed (the first sdr_open()).
typedef struct sdr_esp_usb_config {
    size_t ring_bytes;     ///< driver pull ring size in bytes (must be even), 0 for the driver default
    uint8_t task_priority; ///< USB task priority, 0 for the driver default
    int task_core;         ///< USB task core (0 or 1), -1 for the driver default
} sdr_esp_usb_config_t;

/// Set the USB driver tuning; call before rtl_433 opens the input.
void sdr_esp_set_usb_config(sdr_esp_usb_config_t const *config);

/// Stream counters of the open dongle.
typedef struct sdr_esp_stats {
    uint32_t effective_sps;  ///< samples per second actually delivered
    uint32_t sample_rate;    ///< programmed sample rate
    uint32_t frequency;      ///< tuned center frequency
    uint32_t overruns;       ///< USB transfers that could not be delivered
    uint64_t ring_overruns;  ///< bytes overwritten because rtl_433 did not read them in time
    uint64_t bytes_total;    ///< bytes received since the stream started
} sdr_esp_stats_t;

/// Read the stream counters. Safe from any task.
/// @return 1 with @p out filled while a dongle is open, 0 otherwise
int sdr_esp_get_stats(sdr_esp_stats_t *out);

/* For sdr.c */

typedef struct sdr_esp sdr_esp_t;

/// Open the dongle. @p dev_query is "esp", "esp:SERIAL" or ":SERIAL"; anything else uses the first dongle.
/// The first open waits a while for the dongle to enumerate, later opens (restarts) only look.
/// @p dev_info gets a malloc'ed JSON description, owned by the caller.
int sdr_esp_open(sdr_esp_t **out, char const *dev_query, char **dev_info);
void sdr_esp_close(sdr_esp_t *esp);

int sdr_esp_set_center_freq(sdr_esp_t *esp, uint32_t freq);
uint32_t sdr_esp_get_center_freq(sdr_esp_t *esp);
int sdr_esp_set_freq_correction(sdr_esp_t *esp, int ppm);
/// @p gain in tenths of a dB, negative for automatic gain.
int sdr_esp_set_gain(sdr_esp_t *esp, int gain);
int sdr_esp_set_sample_rate(sdr_esp_t *esp, uint32_t rate);
uint32_t sdr_esp_get_sample_rate(sdr_esp_t *esp);
/// Settings as for sdr_apply_settings(): "biastee" and "digital_agc".
int sdr_esp_apply_settings(sdr_esp_t *esp, char const *sdr_settings);

/// Deliver blocks to @p cb until @p keep_going returns 0 or the stream fails (unplug, USB fault).
/// @return 0 when asked to stop or the stream ended, -1 if it could not start
int sdr_esp_read_loop(sdr_esp_t *esp, sdr_event_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len,
        int (*keep_going)(void *), void *keep_going_ctx);

#endif /* INCLUDE_SDR_ESP_H_ */
