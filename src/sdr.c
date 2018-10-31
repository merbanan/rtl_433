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

#include <stdio.h>
#include <stdlib.h>
#include "sdr.h"
#include "rtl-sdr.h"
#include "util.h"

struct sdr_dev {
    rtlsdr_dev_t *rtlsdr_dev;
    int sample_size;
    int running;
};

int sdr_open(sdr_dev_t **out_dev, int *sample_size, char const *dev_query, int verbose)
{
    uint32_t device_count = rtlsdr_get_device_count();
    if (!device_count) {
        fprintf(stderr, "No supported devices found.\n");
        return -1;
    }

    if (verbose)
        fprintf(stderr, "Found %d device(s)\n\n", device_count);

    int dev_index = 0;
    // select rtlsdr device by serial (-d :<serial>)
    if (dev_query && *dev_query == ':') {
        dev_index = rtlsdr_get_index_by_serial(&dev_query[1]);
        if (dev_index < 0) {
            if (verbose)
                fprintf(stderr, "Could not find device with serial '%s' (err %d)",
                        &dev_query[1], dev_index);
            return -1;
        }
    }

    // select rtlsdr device by number (-d <n>)
    else if (dev_query) {
        dev_index = atoi(dev_query);
        // check if 0 is a parsing error?
        if (dev_index < 0) {
            // select first available rtlsdr device
            dev_index = 0;
            dev_query = NULL;
        }
    }

    char vendor[256] = "n/a", product[256] = "n/a", serial[256] = "n/a";
    int r = -1;
    sdr_dev_t *dev = calloc(1, sizeof(sdr_dev_t));

    for (uint32_t i = dev_query ? dev_index : 0;
            //cast quiets -Wsign-compare; if dev_index were < 0, would have returned -1 above
            i < (dev_query ? (unsigned)dev_index + 1 : device_count);
            i++) {
        rtlsdr_get_device_usb_strings(i, vendor, product, serial);

        if (verbose)
            fprintf(stderr, "trying device  %d:  %s, %s, SN: %s\n",
                    i, vendor, product, serial);

        r = rtlsdr_open(&dev->rtlsdr_dev, i);
        if (r < 0) {
            if (verbose)
                fprintf(stderr, "Failed to open rtlsdr device #%d.\n\n", i);
        } else {
            if (verbose)
                fprintf(stderr, "Using device %d: %s\n",
                        i, rtlsdr_get_device_name(i));
            dev->sample_size = sizeof(uint8_t); // CU8
            *sample_size = sizeof(uint8_t); // CU8
            break;
        }
    }
    if (r < 0) {
        free(dev);
        if (verbose)
            fprintf(stderr, "Unable to open a device\n");
    } else {
        *out_dev = dev;
    }

    return r;
}

int sdr_close(sdr_dev_t *dev)
{
    return rtlsdr_close(dev->rtlsdr_dev);
}

int sdr_set_center_freq(sdr_dev_t *dev, uint32_t freq, int verbose)
{
    int r = rtlsdr_set_center_freq(dev->rtlsdr_dev, freq);
    if (verbose) {
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to set center freq.\n");
        else
            fprintf(stderr, "Tuned to %s.\n", nice_freq(sdr_get_center_freq(dev)));
    }
    return r;
}

uint32_t sdr_get_center_freq(sdr_dev_t *dev)
{
    return rtlsdr_get_center_freq(dev->rtlsdr_dev);
}

int sdr_set_freq_correction(sdr_dev_t *dev, int ppm, int verbose)
{
    int r = rtlsdr_set_freq_correction(dev->rtlsdr_dev, ppm);
    if (verbose) {
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to set frequency correction.\n");
        else
            fprintf(stderr, "Frequency correction set to %d.\n", ppm);
    }
    return r;
}

int sdr_set_auto_gain(sdr_dev_t *dev, int verbose)
{
    int r = rtlsdr_set_tuner_gain_mode(dev->rtlsdr_dev, 0);
    if (verbose) {
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to enable automatic gain.\n");
        else
            fprintf(stderr, "Tuner gain set to Auto.\n");
    }
    return r;
}

int sdr_set_tuner_gain(sdr_dev_t *dev, int gain, int verbose)
{
    /* Enable manual gain */
    int r = rtlsdr_set_tuner_gain_mode(dev->rtlsdr_dev, 1);
    if (verbose)
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to enable manual gain.\n");

    /* Set the tuner gain */
    r = rtlsdr_set_tuner_gain(dev->rtlsdr_dev, gain);
    if (verbose) {
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
        else
            fprintf(stderr, "Tuner gain set to %f dB.\n", gain / 10.0);
    }
    return r;
}

int sdr_set_sample_rate(sdr_dev_t *dev, uint32_t rate, int verbose)
{
    int r = rtlsdr_set_sample_rate(dev->rtlsdr_dev, rate);
    if (verbose) {
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to set sample rate.\n");
        else
            fprintf(stderr, "Sample rate set to %d.\n", sdr_get_sample_rate(dev)); // Unfortunately, doesn't return real rate
    }
    return r;
}
uint32_t sdr_get_sample_rate(sdr_dev_t *dev)
{
    return rtlsdr_get_sample_rate(dev->rtlsdr_dev);
}

int sdr_reset(sdr_dev_t *dev)
{
    return rtlsdr_reset_buffer(dev->rtlsdr_dev);
}

int sdr_start(sdr_dev_t *dev, sdr_read_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len)
{
    return rtlsdr_read_async(dev->rtlsdr_dev, cb, ctx, buf_num, buf_len);
}

int sdr_stop(sdr_dev_t *dev)
{
    return rtlsdr_cancel_async(dev->rtlsdr_dev);
}
