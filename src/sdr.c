/**
 * SDR input from RTLSDR or SoapySDR
 *
 * Copyright (C) 2018 Christian Zuckschwerdt
 * based on code
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2014 by Kyle Keen <keenerd@gmail.com>
 * Copyright (C) 2016 by Robert X. Seger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdr.h"
#include "util.h"
#ifdef RTLSDR
#include "rtl-sdr.h"
#endif
#ifdef SOAPYSDR
#include "optparse.h"
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Logger.h>
#endif

struct sdr_dev {
#ifdef SOAPYSDR
    SoapySDRDevice *soapy_dev;
    SoapySDRStream *soapy_stream;
    double fullScale;
    int running;
#endif
#ifdef RTLSDR
    rtlsdr_dev_t *rtlsdr_dev;
#endif
    int sample_size;
};

/* RTL-SDR helpers */

#ifdef RTLSDR

static int sdr_open_rtl(sdr_dev_t **out_dev, int *sample_size, char *dev_query, int verbose)
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

#endif

/* SoapySDR helpers */

#ifdef SOAPYSDR

static int soapysdr_set_bandwidth(SoapySDRDevice *dev, uint32_t bandwidth)
{
    int r;
    r = (int)SoapySDRDevice_setBandwidth(dev, SOAPY_SDR_RX, 0, (double)bandwidth);
    uint32_t applied_bw = 0;
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to set bandwidth.\n");
    } else if (bandwidth > 0) {
        applied_bw = (uint32_t)SoapySDRDevice_getBandwidth(dev, SOAPY_SDR_RX, 0);
        if (applied_bw)
            fprintf(stderr, "Bandwidth parameter %u Hz resulted in %u Hz.\n", bandwidth, applied_bw);
        else
            fprintf(stderr, "Set bandwidth parameter %u Hz.\n", bandwidth);
    } else {
        fprintf(stderr, "Bandwidth set to automatic resulted in %u Hz.\n", applied_bw);
    }
    return r;
}

static int soapysdr_direct_sampling(SoapySDRDevice *dev, int on)
{
    int r = 0;
    char *value, *set_value;
    if (on == 0)
        value = "0";
    else if (on == 1)
        value = "1";
    else if (on == 2)
        value = "2";
    else
        return -1;
    SoapySDRDevice_writeSetting(dev, "direct_samp", value);
    set_value = SoapySDRDevice_readSetting(dev, "direct_samp");

    if (set_value == NULL) {
        fprintf(stderr, "WARNING: Failed to set direct sampling mode.\n");
        return r;
    }
    if (atoi(set_value) == 0) {
        fprintf(stderr, "Direct sampling mode disabled.\n");}
    if (atoi(set_value) == 1) {
        fprintf(stderr, "Enabled direct sampling mode, input 1/I.\n");}
    if (atoi(set_value) == 2) {
        fprintf(stderr, "Enabled direct sampling mode, input 2/Q.\n");}
    if (on == 3) {
        fprintf(stderr, "Enabled no-mod direct sampling mode.\n");}
    return r;
}

static int soapysdr_offset_tuning(SoapySDRDevice *dev)
{
    int r = 0;
    SoapySDRDevice_writeSetting(dev, "offset_tune", "true");
    char *set_value = SoapySDRDevice_readSetting(dev, "offset_tune");

    if (strcmp(set_value, "true") != 0) {
        /* TODO: detection of failure modes
        if ( r == -2 )
            fprintf(stderr, "WARNING: Failed to set offset tuning: tuner doesn't support offset tuning!\n");
        else if ( r == -3 )
            fprintf(stderr, "WARNING: Failed to set offset tuning: direct sampling not combinable with offset tuning!\n");
        else
        */
            fprintf(stderr, "WARNING: Failed to set offset tuning.\n");
    } else {
        fprintf(stderr, "Offset tuning mode enabled.\n");
    }
    return r;
}

static int soapysdr_auto_gain(SoapySDRDevice *dev, int verbose)
{
    int r = 0;

    r = SoapySDRDevice_hasGainMode(dev, SOAPY_SDR_RX, 0);
    if (r) {
        r = SoapySDRDevice_setGainMode(dev, SOAPY_SDR_RX, 0, 1);
        if (r != 0) {
            fprintf(stderr, "WARNING: Failed to enable automatic gain.\n");
        } else {
            if (verbose)
                fprintf(stderr, "Tuner set to automatic gain.\n");
        }
    }

    // Per-driver hacks TODO: clean this up
    char *driver = SoapySDRDevice_getDriverKey(dev);
    if (strcmp(driver, "HackRF") == 0) {
        // HackRF has three gains LNA, VGA, and AMP, setting total distributes amongst, 116.0 dB seems to work well,
        // even though it logs HACKRF_ERROR_INVALID_PARAM? https://github.com/rxseger/rx_tools/issues/9
        // Total gain is distributed amongst all gains, 116 = 37,65,1; the LNA is OK (<40) but VGA is out of range (65 > 62)
        // TODO: generic means to set all gains, of any SDR? string parsing LNA=#,VGA=#,AMP=#?
        r = SoapySDRDevice_setGainElement(dev, SOAPY_SDR_RX, 0, "LNA", 40.); // max 40
        if (r != 0) {
            fprintf(stderr, "WARNING: Failed to set LNA tuner gain.\n");
        }
        r = SoapySDRDevice_setGainElement(dev, SOAPY_SDR_RX, 0, "VGA", 20.); // max 65
        if (r != 0) {
            fprintf(stderr, "WARNING: Failed to set VGA tuner gain.\n");
        }
        r = SoapySDRDevice_setGainElement(dev, SOAPY_SDR_RX, 0, "AMP", 0.); // on or off
        if (r != 0) {
            fprintf(stderr, "WARNING: Failed to set AMP tuner gain.\n");
        }

    }
    // otherwise leave unset, hopefully the driver has good defaults

    return r;
}

static int soapysdr_gain_str_set(SoapySDRDevice *dev, char *gain_str, int verbose)
{
    SoapySDRKwargs args = {0};
    int r = 0;

    // Disable automatic gain
    r = SoapySDRDevice_hasGainMode(dev, SOAPY_SDR_RX, 0);
    if (r) {
        r = SoapySDRDevice_setGainMode(dev, SOAPY_SDR_RX, 0, 0);
        if (r != 0) {
            fprintf(stderr, "WARNING: Failed to disable automatic gain.\n");
        } else {
            if (verbose)
                fprintf(stderr, "Tuner set to manual gain.\n");
        }
    }

    if (strchr(gain_str, '=')) {
        // Set each gain individually (more control)
        char *name;
        char *value;
        while (getkwargs(&gain_str, &name, &value)) {
            double num = atof(value);
            if (verbose)
                fprintf(stderr, "Setting gain element %s: %f dB\n", name, num);
            r = SoapySDRDevice_setGainElement(dev, SOAPY_SDR_RX, 0, name, num);
            if (r != 0) {
                fprintf(stderr, "WARNING: setGainElement(%s, %f) failed: %d\n", name, num, r);
            }
        }
    } else {
        // Set overall gain and let SoapySDR distribute amongst components
        double value = atof(gain_str);
        r = SoapySDRDevice_setGain(dev, SOAPY_SDR_RX, 0, value);
        if (r != 0) {
            fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
        } else {
            if (verbose)
                fprintf(stderr, "Tuner gain set to %0.2f dB.\n", value);
        }
        // read back and print each individual gain element
        if (verbose) {
            size_t len = 0;
            char **gains = SoapySDRDevice_listGains(dev, SOAPY_SDR_RX, 0, &len);
            fprintf(stderr, "Gain elements: ");
            for (size_t i = 0; i < len; ++i) {
                double gain = SoapySDRDevice_getGain(dev, SOAPY_SDR_RX, 0);
                fprintf(stderr, "%s=%g ", gains[i], gain);
            }
            fprintf(stderr, "\n");
        }
    }

    return r;
}

static void soapysdr_show_device_info(SoapySDRDevice *dev)
{
    size_t len = 0, i = 0;
    char **antennas = NULL;
    char **gains = NULL;
    char **frequencies = NULL;
    SoapySDRRange *frequencyRanges = NULL;
    SoapySDRRange *rates = NULL;
    SoapySDRRange *bandwidths = NULL;
    double fullScale;
    char **stream_formats = NULL;
    char *native_stream_format = NULL;
    SoapySDRKwargs args;
    char *hwkey = NULL;

    int direction = SOAPY_SDR_RX;
    size_t channel = 0;

    hwkey = SoapySDRDevice_getHardwareKey(dev);
    fprintf(stderr, "Using device %s: ", hwkey);

    args = SoapySDRDevice_getHardwareInfo(dev);
    for (i = 0; i < args.size; ++i) {
        fprintf(stderr, "%s=%s ", args.keys[i], args.vals[i]);
    }
    fprintf(stderr, "\n");

    antennas = SoapySDRDevice_listAntennas(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu antenna(s): ", len);
    for (i = 0; i < len; ++i) {
        fprintf(stderr, "%s ", antennas[i]);
    }
    fprintf(stderr, "\n");

    gains = SoapySDRDevice_listGains(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu gain(s): ", len);
    for (i = 0; i < len; ++i) {
        SoapySDRRange gainRange = SoapySDRDevice_getGainRange(dev, direction, channel);
        fprintf(stderr, "%s %.0f - %.0f (step %.0f) ", gains[i], gainRange.minimum, gainRange.maximum, gainRange.step);
    }
    fprintf(stderr, "\n");

    frequencies = SoapySDRDevice_listFrequencies(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu frequencies: ", len);
    for (i = 0; i < len; ++i) {
        fprintf(stderr, "%s ", frequencies[i]);
    }
    fprintf(stderr, "\n");

    frequencyRanges = SoapySDRDevice_getFrequencyRange(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu frequency range(s): ", len);
    for (i = 0; i < len; ++i) {
        fprintf(stderr, "%.0f - %.0f (step %.0f) ", frequencyRanges[i].minimum, frequencyRanges[i].maximum, frequencyRanges[i].step);
    }
    fprintf(stderr, "\n");

    rates = SoapySDRDevice_getSampleRateRange(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu sample rate range(s): ", len);
    for (i = 0; i < len; ++i) {
        if (rates[i].minimum == rates[i].maximum)
            fprintf(stderr, "%.0f ", rates[i].minimum);
        else
            fprintf(stderr, "%.0f - %.0f (step %.0f) ", rates[i].minimum, rates[i].maximum, rates[i].step);
    }
    fprintf(stderr, "\n");

    bandwidths = SoapySDRDevice_getBandwidthRange(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu bandwidth range(s): ", len);
    for (i = 0; i < len; ++i) {
        fprintf(stderr, "%.0f - %.0f (step %.0f) ", bandwidths[i].minimum, bandwidths[i].maximum, bandwidths[i].step);
    }
    fprintf(stderr, "\n");

    double bandwidth = SoapySDRDevice_getBandwidth(dev, direction, channel);
    fprintf(stderr, "Found current bandwidth %.0f\n", bandwidth);

    stream_formats = SoapySDRDevice_getStreamFormats(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu stream format(s): ", len);
    for (i = 0; i < len; ++i) {
        fprintf(stderr, "%s ", stream_formats[i]);
    }
    fprintf(stderr, "\n");

    native_stream_format = SoapySDRDevice_getNativeStreamFormat(dev, direction, channel, &fullScale);
    fprintf(stderr, "Found native stream format: %s (full scale: %.1f)\n", native_stream_format, fullScale);
}

static int sdr_open_soapy(sdr_dev_t **out_dev, int *sample_size, char *dev_query, int verbose)
{
    if (verbose)
        SoapySDR_setLogLevel(SOAPY_SDR_DEBUG);

    sdr_dev_t *dev = calloc(1, sizeof(sdr_dev_t));

    dev->soapy_dev = SoapySDRDevice_makeStrArgs(dev_query);
    if (!dev->soapy_dev) {
        if (verbose)
            fprintf(stderr, "Failed to open sdr device matching '%s'.\n", dev_query);
        free(dev);
        return -1;
    }

    if (verbose)
        soapysdr_show_device_info(dev->soapy_dev);

    // select a stream format, in preference order: native CU8, CS8, CS16, forced CS16
    // stream_formats = SoapySDRDevice_getStreamFormats(dev->soapy_dev, SOAPY_SDR_RX, 0, &len);
    char *format = SoapySDRDevice_getNativeStreamFormat(dev->soapy_dev, SOAPY_SDR_RX, 0, &dev->fullScale);
    if (!strcmp(SOAPY_SDR_CU8, format)) {
        // actually not supported by SoapySDR
        *sample_size = sizeof(uint8_t); // CU8
//    } else if (!strcmp(SOAPY_SDR_CS8, format)) {
//        // TODO: CS8 needs conversion to CU8
//        // e.g. RTL-SDR (8 bit), scale is 128.0
//        *sample_size = sizeof(int8_t); // CS8
    } else if (!strcmp(SOAPY_SDR_CS16, format)) {
        // e.g. LimeSDR-mini (12 bit), scale is 2048.0
        *sample_size = sizeof(int16_t); // CS16
    } else {
        // force CS16
        format = SOAPY_SDR_CS16;
        *sample_size = sizeof(int16_t); // CS16
        dev->fullScale = 32768.0; // assume max for SOAPY_SDR_CS16
    }

    SoapySDRKwargs stream_args = {0};
    if (SoapySDRDevice_setupStream(dev->soapy_dev, &dev->soapy_stream, SOAPY_SDR_RX, format, NULL, 0, &stream_args) != 0) {
        if (verbose)
            fprintf(stderr, "Failed to setup sdr device\n");
        free(dev);
        return -3;
    }

    *out_dev = dev;
    return 0;
}

static int soapysdr_read_loop(sdr_dev_t *dev, sdr_read_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len)
{
    size_t buf_elems = buf_len / 2; // div sizeof int16_t, or demod->sample_size, maybe?
    int16_t *buffer  = malloc(buf_elems * SoapySDR_formatToSize(SOAPY_SDR_CS16));

    dev->running = 1;
    do {
        void *buffs[]    = {buffer};
        int flags        = 0;
        long long timeNs = 0;
        long timeoutUs   = 1000000; // 1 second
        unsigned n_read  = 0, i;
        int r;

        do {
            buffs[0] = &buffer[n_read * 2];
            r  = SoapySDRDevice_readStream(dev->soapy_dev, dev->soapy_stream, buffs, buf_elems - n_read, &flags, &timeNs, timeoutUs);
            if (r < 0)
                break;
            n_read += r; // r is number of elements read, elements=complex pairs, so buffer length is twice
            //fprintf(stderr, "readStream ret=%d, flags=%d, timeNs=%lld (%zu - %u)\n", r, flags, timeNs, buf_elems, n_read);
        } while (n_read < buf_elems);
        //fprintf(stderr, "readStream ret=%d (%d), flags=%d, timeNs=%lld\n", n_read, buf_len, flags, timeNs);
        if (r < 0) {
            if (r == SOAPY_SDR_OVERFLOW) {
                fprintf(stderr, "O");
                fflush(stderr);
                continue;
            }
            fprintf(stderr, "WARNING: sync read failed. %d\n", r);
        }

        // convert to CS16 or CU8 if needed
        // if converting CS8 to CU8 -- vectorized with -O3
        //for (i = 0; i < n_read * 2; ++i)
        //    cu8buf[i] = (int8_t)cu8buf[i] + 128;

        // rescale cs16 buffer
        if (dev->fullScale == 2048.0) {
            for (i = 0; i < n_read * 2; ++i)
                buffer[i] <<= 4;
        } else if (dev->fullScale != 32768.0) {
            int upscale = 32768 / dev->fullScale;
            for (i = 0; i < n_read * 2; ++i)
                buffer[i] *= upscale;
        }

        if (n_read > 0) // prevent a crash in callback
            cb((unsigned char *)buffer, n_read * 2 * 2, ctx);

    } while (dev->running);
    return 0;
}

#endif

/* Public API */

int sdr_open(sdr_dev_t **out_dev, int *sample_size, char *dev_query, int verbose)
{
#if !defined(RTLSDR) && !defined(SOAPYSDR)
    if (verbose)
        fprintf(stderr, "No input drivers (RTL-SDR or SoapySDR) compiled in.\n");
    return -1;
#endif
#ifdef RTLSDR
    /* Open RTLSDR by default or if index or serial given, if available */
    if (!dev_query || *dev_query == ':' || (*dev_query >= '0' && *dev_query <= '9'))
        return sdr_open_rtl(out_dev, sample_size, dev_query, verbose);
#endif
#ifdef SOAPYSDR
    /* Open SoapySDR otherwise, if available */
    return sdr_open_soapy(out_dev, sample_size, dev_query, verbose);
#endif
    return -1;
}

int sdr_close(sdr_dev_t *dev)
{
#ifdef SOAPYSDR
    if (dev->soapy_dev)
        return SoapySDRDevice_unmake(dev->soapy_dev);
#endif
#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        return rtlsdr_close(dev->rtlsdr_dev);
#endif
    return -1;
}

int sdr_set_center_freq(sdr_dev_t *dev, uint32_t freq, int verbose)
{
    int r = -1;
#ifdef SOAPYSDR
    SoapySDRKwargs args = {0};
    if (dev->soapy_dev) {
        r = SoapySDRDevice_setFrequency(dev->soapy_dev, SOAPY_SDR_RX, 0, (double)freq, &args);
    }
#endif
#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        r = rtlsdr_set_center_freq(dev->rtlsdr_dev, freq);
#endif
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
#ifdef SOAPYSDR
    if (dev->soapy_dev)
        return (int)SoapySDRDevice_getFrequency(dev->soapy_dev, SOAPY_SDR_RX, 0);
#endif
#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        return rtlsdr_get_center_freq(dev->rtlsdr_dev);
#endif
    return 0;
}

int sdr_set_freq_correction(sdr_dev_t *dev, int ppm, int verbose)
{
    int r = -1;
#ifdef SOAPYSDR
    if (dev->soapy_dev)
        r = SoapySDRDevice_setFrequencyComponent(dev->soapy_dev, SOAPY_SDR_RX, 0, "CORR", (double)ppm, NULL);
#endif
#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        r = rtlsdr_set_freq_correction(dev->rtlsdr_dev, ppm);
#endif
    if (verbose) {
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to set frequency correction.\n");
        else
            fprintf(stderr, "Frequency correction set to %d ppm.\n", ppm);
    }
    return r;
}

int sdr_set_auto_gain(sdr_dev_t *dev, int verbose)
{
    int r = -1;
#ifdef SOAPYSDR
    if (dev->soapy_dev)
        r = soapysdr_auto_gain(dev->soapy_dev, verbose);
#endif
#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        r = rtlsdr_set_tuner_gain_mode(dev->rtlsdr_dev, 0);
#endif
    if (verbose) {
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to enable automatic gain.\n");
        else
            fprintf(stderr, "Tuner gain set to Auto.\n");
    }
    return r;
}

int sdr_set_tuner_gain(sdr_dev_t *dev, char *gain_str, int verbose)
{
    if (!gain_str || !*gain_str) {
        /* Enable automatic gain */
        return sdr_set_auto_gain(dev, verbose);
    }
#ifdef SOAPYSDR
    /* Enable manual gain */
    if (dev->soapy_dev)
        return soapysdr_gain_str_set(dev->soapy_dev, gain_str, verbose);
#endif
#ifdef RTLSDR
    int gain = (int)(atof(gain_str) * 10); /* tenths of a dB */
    if (gain == 0) {
        /* Enable automatic gain */
        return sdr_set_auto_gain(dev, verbose);
    }

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
#endif
    return -1;
}

int sdr_set_sample_rate(sdr_dev_t *dev, uint32_t rate, int verbose)
{
    int r = -1;
#ifdef SOAPYSDR
    if (dev->soapy_dev)
        r = SoapySDRDevice_setSampleRate(dev->soapy_dev, SOAPY_SDR_RX, 0, (double)rate);
#endif
#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        r = rtlsdr_set_sample_rate(dev->rtlsdr_dev, rate);
#endif
    if (verbose) {
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to set sample rate.\n");
        else
            fprintf(stderr, "Sample rate set to %d S/s.\n", sdr_get_sample_rate(dev)); // Unfortunately, doesn't return real rate
    }
    return r;
}

uint32_t sdr_get_sample_rate(sdr_dev_t *dev)
{
#ifdef SOAPYSDR
    if (dev->soapy_dev)
        return (int)SoapySDRDevice_getSampleRate(dev->soapy_dev, SOAPY_SDR_RX, 0);
#endif
#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        return rtlsdr_get_sample_rate(dev->rtlsdr_dev);
#endif
    return 0;
}

int sdr_activate(sdr_dev_t *dev)
{
#ifdef SOAPYSDR
    if (dev->soapy_dev) {
        if (SoapySDRDevice_activateStream(dev->soapy_dev, dev->soapy_stream, 0, 0, 0) != 0) {
            fprintf(stderr, "Failed to activate stream\n");
            exit(1);
        }
    }
#endif
    return 0;
}

int sdr_deactivate(sdr_dev_t *dev)
{
#ifdef SOAPYSDR
    if (dev->soapy_dev) {
        SoapySDRDevice_deactivateStream(dev->soapy_dev, dev->soapy_stream, 0, 0);
        SoapySDRDevice_closeStream(dev->soapy_dev, dev->soapy_stream);
    }
#endif
    return 0;
}

int sdr_reset(sdr_dev_t *dev, int verbose)
{
    int r = 0;
#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        r = rtlsdr_reset_buffer(dev->rtlsdr_dev);
#endif
    if (verbose) {
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to reset buffers.\n");
    }
    return r;
}

int sdr_start(sdr_dev_t *dev, sdr_read_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len)
{
#ifdef SOAPYSDR
    if (dev->soapy_dev)
        return soapysdr_read_loop(dev, cb, ctx, buf_num, buf_len);
#endif
#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        return rtlsdr_read_async(dev->rtlsdr_dev, cb, ctx, buf_num, buf_len);
#endif
    return -1;
}

int sdr_stop(sdr_dev_t *dev)
{
#ifdef SOAPYSDR
    if (dev->soapy_dev) {
        dev->running = 0;
        return 0;
    }
#endif
#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        return rtlsdr_cancel_async(dev->rtlsdr_dev);
#endif
    return -1;
}
