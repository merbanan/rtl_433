/** @file
    ESP-IDF input: an RTL2832U dongle on the USB host port of an ESP32-P4.

    Uses the esp_rtl_sdr driver component. Only built with ESP_RTL_SDR.
    esp_rtl_sdr is AGPL-3.0-only, so a firmware that links it is AGPL-3.0 as a whole.

    Copyright (C) 2026 David Coulson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "sdr_esp.h"

#ifdef ESP_RTL_SDR

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_rtl_sdr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "r_util.h"
#include "optparse.h"
#include "logger.h"
#include "fatal.h"
#include "compat_pthread.h"

#define ESP_SDR_DEFAULT_FREQ 433920000u
#define ESP_SDR_DEFAULT_RATE 250000u
#define ESP_SDR_ATTACH_WAIT_MS 15000
#define ESP_SDR_READ_TIMEOUT_MS 500
#define ESP_SDR_NOT_STREAMING_MAX_MS 5000 // longer than the stop and start a rate change can need
#define ESP_SDR_START_BACKOFF_MAX_MS 30000

struct sdr_esp {
    esp_rtl_sdr_handle_t handle;
    pthread_mutex_t lock; ///< guards freq, rate and tune_gen
    uint32_t freq;        ///< center frequency, applied at stream start and labelling the blocks
    uint32_t rate;        ///< sample rate, applied at stream start and labelling the blocks
    uint32_t tune_gen;    ///< bumped by every retune or rate change
    int gain;             ///< tuner gain in tenths of dB, < 0 for automatic
    int ppm;
    int biastee;     ///< -1 to leave as is
    int digital_agc; ///< -1 to leave as is
    volatile int streaming;
    uint8_t *buffer;
    size_t buffer_size;
    size_t buffer_pos;
};

static sdr_esp_usb_config_t usb_config = {0, 0, -1};

void sdr_esp_set_usb_config(sdr_esp_usb_config_t const *config)
{
    usb_config = *config;
}

// The driver, and the USB host library under it, is installed once and kept: uninstalling with a dongle
// attached fails in the host library, and installing again after that fails too. A restart or a replug
// only stops and starts the stream, the driver's own rescan handles the dongle coming back.
static esp_rtl_sdr_handle_t esp_handle = NULL;
static int esp_cleanup_pending = 0; // esp_handle is the cleanup-only handle of a failed install
static int esp_opened_before   = 0; // only the first open (start-up) waits for the dongle
static int esp_absent_logged   = 0; // "no dongle" is logged once per absence, not on every retry

// The open handle, for sdr_esp_get_stats() from other tasks
static pthread_mutex_t esp_active_lock = PTHREAD_MUTEX_INITIALIZER;
static esp_rtl_sdr_handle_t esp_active = NULL;

int sdr_esp_get_stats(sdr_esp_stats_t *out)
{
    int ok = 0;
    pthread_mutex_lock(&esp_active_lock);
    if (esp_active) {
        esp_rtl_sdr_metrics_t m;
        if (esp_rtl_sdr_get_metrics(esp_active, &m) == ESP_OK) {
            out->effective_sps = m.effective_sps;
            out->sample_rate   = m.sample_rate_sps;
            out->frequency     = m.frequency_hz;
            out->overruns      = m.overruns;
            out->ring_overruns = m.ring_overrun_bytes;
            out->bytes_total   = m.bytes_total;
            ok = 1;
        }
    }
    pthread_mutex_unlock(&esp_active_lock);
    return ok;
}

// The driver's event callback runs on its USB task, which must not touch a sdr_esp_t: the input can be
// closed (freed) at any moment. It only counts, and the read loop compares the counts.
static volatile uint32_t esp_disconnects = 0;
// EVT_ERROR: the stream died (FAULT), an unplugged dongle could not be closed yet, or a retune failed and
// the driver kept the previous frequency (the samples are then not where they are labelled). The read
// loop ends on a change, and rtl_433 restarts the input at the requested settings.
static volatile uint32_t esp_errors = 0;

static void esp_sdr_event_cb(esp_rtl_sdr_event_t event, const void *payload, void *ctx)
{
    UNUSED(ctx);
    switch (event) {
    case ESP_RTL_SDR_EVT_ENUMERATED: {
        esp_rtl_sdr_device_info_t const *info = payload;
        if (info)
            print_logf(LOG_NOTICE, "SDR", "USB SDR attached: %s %s, SN: %s (%s speed)",
                    info->manufacturer, info->product, info->serial, info->high_speed ? "high" : "full");
        break;
    }
    case ESP_RTL_SDR_EVT_DISCONNECTED:
        esp_disconnects++; // first: the log below may block
        print_log(LOG_ERROR, "SDR", "USB SDR disconnected");
        break;
    case ESP_RTL_SDR_EVT_ERROR: {
        esp_errors++; // first: the log below may block
        esp_rtl_sdr_error_info_t const *err = payload;
        if (err)
            print_logf(LOG_WARNING, "SDR", "esp_rtl_sdr: %s %s", esp_rtl_sdr_err_to_name(err->code), err->message);
        break;
    }
    default:
        break;
    }
}

static int esp_sdr_install(void)
{
    if (esp_cleanup_pending) {
        // A failed install that could not unwind returned this handle: it is the only way to release what
        // the driver kept (its tasks, USB ownership), so finish that before installing again
        if (esp_rtl_sdr_uninstall(esp_handle) != ESP_OK) {
            print_log(LOG_ERROR, __func__, "esp_rtl_sdr_uninstall of a failed install did not finish, will retry");
            return -1;
        }
        esp_handle          = NULL;
        esp_cleanup_pending = 0;
    }
    if (esp_handle)
        return 0;

    esp_rtl_sdr_config_t config;
    esp_rtl_sdr_config_default(&config);
    if (usb_config.ring_bytes)
        config.pull_ring_bytes = usb_config.ring_bytes;
    if (usb_config.task_priority)
        config.usb_task_priority = usb_config.task_priority;
    if (usb_config.task_core >= 0)
        config.usb_task_core_id = (uint8_t)usb_config.task_core;
    config.event_cb      = esp_sdr_event_cb;
    config.event_ctx     = NULL;
    config.delivery_mode = ESP_RTL_SDR_DELIVERY_READ; // rtl_433 pulls, as from a socket

    esp_err_t err = esp_rtl_sdr_install(&config, &esp_handle);
    if (err == ESP_RTL_SDR_ERR_USB_SAFE_MODE) {
        // The driver's crash guard: the USB stack panicked during enumeration on several boots in a row,
        // so it stays off for this boot instead of boot-looping
        print_log(LOG_ERROR, __func__, "USB host disabled for this boot after repeated USB crashes, power-cycle to retry");
        esp_handle = NULL;
        return -1;
    }
    if (err != ESP_OK) {
        print_logf(LOG_ERROR, __func__, "esp_rtl_sdr_install failed: %s", esp_rtl_sdr_err_to_name(err));
        esp_cleanup_pending = esp_handle != NULL; // keep a cleanup-only handle, see above
        return -1;
    }
    return 0;
}

int sdr_esp_open(sdr_esp_t **out, char const *dev_query, char **dev_info)
{
    int may_wait      = !esp_opened_before;
    esp_opened_before = 1;

    if (esp_sdr_install() < 0)
        return -1;

    // The dongle enumerates some time after the host library starts, so the first open (start-up) waits
    // for it. Every later open is a restart from rtl_433's event loop, which retries about every 1.5 s:
    // it only looks, because waiting there would stall HTTP and WebSocket while the dongle is missing.
    esp_rtl_sdr_device_info_t info = {0};
    int waited_ms = 0;
    while (esp_rtl_sdr_get_device_info(esp_handle, &info) != ESP_OK || !info.present) {
        if (!may_wait || waited_ms >= ESP_SDR_ATTACH_WAIT_MS) {
            if (!esp_absent_logged)
                print_log(LOG_ERROR, __func__, "No supported RTL-SDR dongle on the USB host port");
            esp_absent_logged = 1;
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        waited_ms += 100;
    }
    esp_absent_logged = 0;

    // "esp:SERIAL" or ":SERIAL" picks one dongle when several share a hub
    char const *serial = arg_param(dev_query);
    if (serial && *serial && *serial != ',') {
        esp_err_t err = esp_rtl_sdr_select_device_serial(esp_handle, serial);
        if (err != ESP_OK)
            print_logf(LOG_WARNING, __func__, "No dongle with serial %s (%s), using the first one",
                    serial, esp_rtl_sdr_err_to_name(err));
        esp_rtl_sdr_get_device_info(esp_handle, &info);
    }

    sdr_esp_t *esp = calloc(1, sizeof(*esp));
    if (!esp) {
        WARN_CALLOC("sdr_esp_open()");
        return -1;
    }
    pthread_mutex_init(&esp->lock, NULL);
    esp->handle      = esp_handle;
    esp->gain        = -1;
    esp->biastee     = -1;
    esp->digital_agc = -1;

    print_logf(LOG_CRITICAL, "SDR", "Using USB device: %s, %s, SN: %s", info.manufacturer, info.product, info.serial);

    size_t info_len = 41 + strlen(info.manufacturer) + strlen(info.product) + strlen(info.serial);
    *dev_info       = malloc(info_len);
    if (!*dev_info) {
        WARN_MALLOC("sdr_esp_open()"); // no device info then
    }
    else {
        snprintf(*dev_info, info_len, "{\"vendor\":\"%s\", \"product\":\"%s\", \"serial\":\"%s\"}",
                info.manufacturer, info.product, info.serial);
    }

    pthread_mutex_lock(&esp_active_lock);
    esp_active = esp->handle;
    pthread_mutex_unlock(&esp_active_lock);

    *out = esp;
    return 0;
}

void sdr_esp_close(sdr_esp_t *esp)
{
    if (!esp)
        return;
    pthread_mutex_lock(&esp_active_lock);
    if (esp_active == esp->handle)
        esp_active = NULL;
    pthread_mutex_unlock(&esp_active_lock);
    esp_rtl_sdr_stop(esp->handle, 0); // the driver stays installed, see esp_handle
    esp->streaming = 0;
    pthread_mutex_destroy(&esp->lock);
    free(esp->buffer);
    free(esp);
}

// Gain can only be written once the interface is claimed, i.e. after the stream starts
static void esp_sdr_apply_gain(sdr_esp_t *esp)
{
    esp_err_t err;
    if (esp->gain < 0)
        err = esp_rtl_sdr_set_tuner_gain_mode(esp->handle, ESP_RTL_SDR_GAIN_MODE_AUTO);
    else
        err = esp_rtl_sdr_set_tuner_gain(esp->handle, esp->gain);
    if (err != ESP_OK)
        print_logf(LOG_WARNING, "SDR", "Setting tuner gain failed: %s", esp_rtl_sdr_err_to_name(err));
}

// Consecutive failed starts back off (1 s, 2 s, 4 s ... 30 s), so a dongle that keeps failing is not sent
// a full init sequence on every restart (the gate is in sdr_esp_read_loop(); the restart of a rate change
// is not throttled). Kept across opens: the sdr_esp_t is per open, the dongle is not.
static unsigned esp_start_failures = 0;
static TickType_t esp_start_not_before = 0;

static int esp_sdr_start_stream(sdr_esp_t *esp)
{
    pthread_mutex_lock(&esp->lock);
    uint32_t freq = esp->freq ? esp->freq : ESP_SDR_DEFAULT_FREQ;
    uint32_t rate = esp->rate ? esp->rate : ESP_SDR_DEFAULT_RATE;
    pthread_mutex_unlock(&esp->lock);

    if (esp->ppm)
        esp_rtl_sdr_set_freq_correction(esp->handle, esp->ppm);
    esp_err_t err = esp_rtl_sdr_start_hz(esp->handle, freq, rate);
    if (err == ESP_RTL_SDR_ERR_FAULT) {
        // A failed start or a dead stream leaves the driver in FAULT, and start() refuses until stop() and
        // reset() clear it. reset() refuses while transfers are still live; the backoff gives them time.
        print_log(LOG_WARNING, "SDR", "USB SDR driver in FAULT, resetting it");
        esp_rtl_sdr_stop(esp->handle, 0);
        esp_err_t rerr = esp_rtl_sdr_reset(esp->handle);
        if (rerr != ESP_OK && esp_rtl_sdr_get_state(esp->handle) != ESP_RTL_SDR_STATE_IDLE)
            print_logf(LOG_WARNING, "SDR", "USB SDR reset failed: %s", esp_rtl_sdr_err_to_name(rerr));
        err = esp_rtl_sdr_start_hz(esp->handle, freq, rate);
    }
    if (err != ESP_OK) {
        unsigned backoff_ms = 1000u << (esp_start_failures < 5 ? esp_start_failures : 5);
        if (backoff_ms > ESP_SDR_START_BACKOFF_MAX_MS)
            backoff_ms = ESP_SDR_START_BACKOFF_MAX_MS;
        esp_start_failures++;
        esp_start_not_before = xTaskGetTickCount() + pdMS_TO_TICKS(backoff_ms);
        print_logf(LOG_ERROR, "SDR", "Starting the USB stream at %u Hz, %u S/s failed: %s (attempt %u, next in %u s)",
                (unsigned)freq, (unsigned)rate, esp_rtl_sdr_err_to_name(err), esp_start_failures, backoff_ms / 1000u);
        esp->streaming = 0;
        return -1;
    }
    esp_start_failures = 0;
    esp->streaming     = 1;
    // These need the claimed interface too
    esp_sdr_apply_gain(esp);
    if (esp->biastee >= 0)
        esp_rtl_sdr_set_bias_tee(esp->handle, esp->biastee != 0);
    if (esp->digital_agc >= 0)
        esp_rtl_sdr_set_rtl_agc(esp->handle, esp->digital_agc != 0);
    return 0;
}

int sdr_esp_set_center_freq(sdr_esp_t *esp, uint32_t freq)
{
    // Idle: remembered for the stream start. Streaming: a retune between bulk transfers. If that fails the
    // driver keeps the old frequency and sends EVT_ERROR, the read loop ends, and the restart tunes here.
    int r = esp_rtl_sdr_set_center_freq(esp->handle, freq) == ESP_OK ? 0 : -1;
    pthread_mutex_lock(&esp->lock);
    esp->freq = freq;
    esp->tune_gen++; // the read loop drops what the driver buffered before this
    pthread_mutex_unlock(&esp->lock);
    return r;
}

uint32_t sdr_esp_get_center_freq(sdr_esp_t *esp)
{
    uint32_t hz = 0;
    esp_rtl_sdr_get_center_freq(esp->handle, &hz);
    if (hz)
        return hz;
    pthread_mutex_lock(&esp->lock);
    hz = esp->freq;
    pthread_mutex_unlock(&esp->lock);
    return hz;
}

int sdr_esp_set_freq_correction(sdr_esp_t *esp, int ppm)
{
    esp->ppm = ppm;
    return esp_rtl_sdr_set_freq_correction(esp->handle, ppm) == ESP_OK ? 0 : -1;
}

int sdr_esp_set_gain(sdr_esp_t *esp, int gain)
{
    esp->gain = gain < 0 ? -1 : gain;
    if (esp->streaming)
        esp_sdr_apply_gain(esp);
    return 0;
}

static void esp_sdr_set_rate_label(sdr_esp_t *esp, uint32_t rate)
{
    pthread_mutex_lock(&esp->lock);
    esp->rate = rate;
    esp->tune_gen++; // as for a retune
    pthread_mutex_unlock(&esp->lock);
}

int sdr_esp_set_sample_rate(sdr_esp_t *esp, uint32_t rate)
{
    if (!esp_rtl_sdr_is_rate_supported(rate)) {
        print_logf(LOG_ERROR, "SDR", "%u S/s is not supported (use 225k-300k or 900k-3.2M)", (unsigned)rate);
        return -1;
    }

    pthread_mutex_lock(&esp->lock);
    uint32_t old_rate = esp->rate;
    pthread_mutex_unlock(&esp->lock);

    if (!esp->streaming) {
        // idle: remembered for the stream start
        if (esp_rtl_sdr_set_sample_rate(esp->handle, rate) != ESP_OK)
            return -1;
        esp_sdr_set_rate_label(esp, rate);
        return 0;
    }
    if (rate == old_rate)
        return 0;

    // A hop to a band with another rate: the driver rewrites the resampler between bulk transfers. If it
    // can't, restart the stream at the new rate now, so the blocks are labelled with the rate they have.
    if (esp_rtl_sdr_set_sample_rate(esp->handle, rate) == ESP_OK) {
        esp_sdr_set_rate_label(esp, rate);
        return 0;
    }
    esp_rtl_sdr_stop(esp->handle, 0);
    esp_sdr_set_rate_label(esp, rate);
    if (esp_sdr_start_stream(esp) == 0)
        return 0;
    esp_sdr_set_rate_label(esp, old_rate); // carry on at the old rate
    esp_sdr_start_stream(esp);
    return -1;
}

uint32_t sdr_esp_get_sample_rate(sdr_esp_t *esp)
{
    pthread_mutex_lock(&esp->lock);
    uint32_t rate = esp->rate;
    pthread_mutex_unlock(&esp->lock);
    return rate;
}

int sdr_esp_apply_settings(sdr_esp_t *esp, char const *sdr_settings)
{
    int r = 0;
    while (sdr_settings && *sdr_settings) {
        char const *val = NULL;
        if (kwargs_match(sdr_settings, "biastee", &val)) {
            esp->biastee = atobv(val, 1);
            if (esp->streaming)
                esp_rtl_sdr_set_bias_tee(esp->handle, esp->biastee != 0);
        }
        else if (kwargs_match(sdr_settings, "digital_agc", &val)) {
            esp->digital_agc = atobv(val, 1);
            if (esp->streaming)
                esp_rtl_sdr_set_rtl_agc(esp->handle, esp->digital_agc != 0);
        }
        else {
            print_logf(LOG_ERROR, __func__, "Unsupported setting for the USB SDR: %s", sdr_settings);
            r = -1;
        }
        sdr_settings = kwargs_skip(sdr_settings);
    }
    return r;
}

// Throw away what the driver has buffered: a retune or rate change does not clear its pull ring, so
// whatever is in it was captured with the old settings
static void esp_sdr_discard_buffered(sdr_esp_t *esp, uint8_t *scratch, size_t len)
{
    size_t got;
    int rounds = 0;
    do {
        got = 0;
        if (esp_rtl_sdr_read(esp->handle, scratch, len, 0, &got) != ESP_OK)
            break; // empty (timeout) or not streaming
    } while (got == len && ++rounds < 64); // a full read: there may be more
}

static uint32_t esp_sdr_labels(sdr_esp_t *esp, uint32_t *freq, uint32_t *rate)
{
    pthread_mutex_lock(&esp->lock);
    uint32_t gen = esp->tune_gen;
    if (freq)
        *freq = esp->freq;
    if (rate)
        *rate = esp->rate;
    pthread_mutex_unlock(&esp->lock);
    return gen;
}

int sdr_esp_read_loop(sdr_esp_t *esp, sdr_event_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len,
        int (*keep_going)(void *), void *keep_going_ctx)
{
    size_t buffer_size = (size_t)buf_num * buf_len;
    if (esp->buffer_size != buffer_size) {
        free(esp->buffer);
        esp->buffer = malloc(buffer_size);
        if (!esp->buffer) {
            WARN_MALLOC("sdr_esp_read_loop()");
            esp->buffer_size = 0;
            return -1;
        }
        esp->buffer_size = buffer_size;
        esp->buffer_pos  = 0;
    }

    if (!esp->streaming) {
        if (esp_start_failures && (int32_t)(xTaskGetTickCount() - esp_start_not_before) < 0)
            return -1; // still backing off, the next restart tries again
        if (esp_sdr_start_stream(esp) < 0)
            return -1;
    }

    // Every block is labelled with the settings it was captured with. A retune or rate change bumps
    // tune_gen once it took effect; samples captured before can still be in the driver's ring, and a hop
    // retunes while the previous block is being decoded. So the next block starts by dropping them, and a
    // block that a change landed in the middle of is dropped whole: about one block per hop.
    uint32_t seen_disconnects = esp_disconnects; // one after this: NOT_STREAMING means unplugged
    uint32_t seen_errors      = esp_errors;      // one after this: a dead or mistuned stream
    uint32_t seen_gen         = esp_sdr_labels(esp, NULL, NULL);

    int ended = 0;
    while (!ended && keep_going(keep_going_ctx)) {
        if (esp->buffer_pos + buf_len > buffer_size)
            esp->buffer_pos = 0;
        uint8_t *buffer = &esp->buffer[esp->buffer_pos];
        esp->buffer_pos += buf_len;

        uint32_t gen = esp_sdr_labels(esp, NULL, NULL);
        if (gen != seen_gen) {
            esp_sdr_discard_buffered(esp, buffer, buf_len);
            seen_gen = gen;
        }

        size_t n_read        = 0;
        int not_streaming_ms = 0;
        while (n_read < buf_len) {
            if (!keep_going(keep_going_ctx))
                return 0;
            if (esp_errors != seen_errors) {
                // Stop delivering; rtl_433 restarts the input at the requested settings
                print_log(LOG_WARNING, "SDR", "USB SDR reported an error, restarting the input");
                esp->streaming = 0;
                ended          = 1;
                break;
            }
            size_t got    = 0;
            esp_err_t err = esp_rtl_sdr_read(esp->handle, &buffer[n_read], buf_len - n_read,
                    ESP_SDR_READ_TIMEOUT_MS, &got);
            if (err == ESP_OK) {
                n_read += got;
                not_streaming_ms = 0;
            }
            else if (err == ESP_RTL_SDR_ERR_NOT_STREAMING && esp->streaming && esp_disconnects == seen_disconnects
                    && not_streaming_ms < ESP_SDR_NOT_STREAMING_MAX_MS) {
                vTaskDelay(pdMS_TO_TICKS(10)); // a rate change is restarting the stream
                not_streaming_ms += 10;
            }
            else if (err != ESP_RTL_SDR_ERR_TIMEOUT && err != ESP_ERR_TIMEOUT) {
                // Unplugged or faulted. Stop delivering; rtl_433 restarts the input.
                print_logf(LOG_WARNING, "SDR", "USB read failed: %s", esp_rtl_sdr_err_to_name(err));
                esp->streaming = 0;
                ended          = 1;
                break;
            }
            // on a timeout go round: no samples yet, check keep_going again
        }
        if (ended)
            break;

        uint32_t center_frequency, sample_rate;
        gen = esp_sdr_labels(esp, &center_frequency, &sample_rate);
        if (gen != seen_gen)
            continue; // the settings changed while this block filled: part of it is the old band
        sdr_event_t ev = {
                .ev               = SDR_EV_DATA,
                .sample_rate      = sample_rate,
                .center_frequency = center_frequency,
                .buf              = buffer,
                .len              = (int)n_read,
        };
        if (n_read > 0)
            cb(&ev, ctx);
    }

    return 0;
}

#endif /* ESP_RTL_SDR */
