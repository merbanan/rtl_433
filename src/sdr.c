/** @file
    SDR input from RTLSDR or SoapySDR.

    Copyright (C) 2018 Christian Zuckschwerdt
    based on code
    Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
    Copyright (C) 2014 by Kyle Keen <keenerd@gmail.com>
    Copyright (C) 2016 by Robert X. Seger

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "sdr.h"
#include "r_util.h"
#include "optparse.h"
#include "logger.h"
#include "fatal.h"
#include "compat_pthread.h"
#ifdef RTLSDR
#include <rtl-sdr.h>
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__))
// not available in rtlsdr 0.5.3, allow weak link for Linux
int __attribute__((weak)) rtlsdr_set_bias_tee(rtlsdr_dev_t *dev, int on);
#endif
#ifdef LIBUSB1
#include <libusb.h> /* libusb_error_name(), libusb_strerror() */
#endif
#endif
#ifdef SOAPYSDR
#include <SoapySDR/Version.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Logger.h>
#if (SOAPY_SDR_API_VERSION < 0x00080000)
#define SoapySDR_free(ptr) free(ptr)
#endif
#endif

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
    #if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600)
    #undef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600   /* Needed to pull in 'struct sockaddr_storage' */
    #endif

    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define SHUT_RDWR SD_BOTH
    #define perror(str)  ws2_perror(str)

    static void ws2_perror (const char *str)
    {
        if (str && *str)
            fprintf(stderr, "%s: ", str);
        fprintf(stderr, "Winsock error %d.\n", WSAGetLastError());
    }
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #include <netinet/in.h>

    #define SOCKET          int
    #define INVALID_SOCKET  (-1)
    #define closesocket(x)  close(x)
#endif

#define GAIN_STR_MAX_SIZE 64

struct sdr_dev {
    SOCKET rtl_tcp;
    uint32_t rtl_tcp_freq; ///< last known center frequency, rtl_tcp only.
    uint32_t rtl_tcp_rate; ///< last known sample rate, rtl_tcp only.

#ifdef SOAPYSDR
    SoapySDRDevice *soapy_dev;
    SoapySDRStream *soapy_stream;
    double fullScale;
#endif

#ifdef RTLSDR
    rtlsdr_dev_t *rtlsdr_dev;
    sdr_event_cb_t rtlsdr_cb;
    void *rtlsdr_cb_ctx;
#endif

    char *dev_info;

    int running;
    uint8_t *buffer; ///< sdr data buffer current and past frames
    size_t buffer_size; ///< sdr data buffer overall size (num * len)
    size_t buffer_pos; ///< sdr data buffer next write position

    int sample_size;
    int sample_signed;

    uint32_t sample_rate;
    uint32_t center_frequency;

#ifdef THREADS
    pthread_t thread;
    pthread_mutex_t lock; ///< lock for exit_acquire
    int exit_acquire;

    // acquire thread args
    sdr_event_cb_t async_cb;
    void *async_ctx;
    uint32_t buf_num;
    uint32_t buf_len;
#endif
};

/* rtl_tcp helpers */

#pragma pack(push, 1)
struct rtl_tcp_info {
    char magic[4];             // "RTL0"
    uint32_t tuner_number;     // big endian
    uint32_t tuner_gain_count; // big endian
};
#pragma pack(pop)

static int rtltcp_open(sdr_dev_t **out_dev, char const *dev_query, int verbose)
{
    UNUSED(verbose);
    char const *host = "localhost";
    char const *port = "1234";
    char hostport[280]; // 253 chars DNS name plus extra chars

    char *param = arg_param(dev_query); // strip scheme
    hostport[0] = '\0';
    if (param)
        strncpy(hostport, param, sizeof(hostport) - 1);
    hostport[sizeof(hostport) - 1] = '\0';
    hostport_param(hostport, &host, &port);

    print_logf(LOG_CRITICAL, "SDR", "rtl_tcp input from %s port %s", host, port);

#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        perror("WSAStartup()");
        return -1;
    }
#endif

    struct addrinfo hints, *res, *res0;
    int ret;
    SOCKET sock;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_flags    = AI_ADDRCONFIG;

    ret = getaddrinfo(host, port, &hints, &res0);
    if (ret) {
        print_log(LOG_ERROR, __func__, gai_strerror(ret));
        return -1;
    }
    sock = INVALID_SOCKET;
    for (res = res0; res; res = res->ai_next) {
        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock >= 0) {
            ret = connect(sock, res->ai_addr, res->ai_addrlen);
            if (ret == -1) {
                perror("connect");
                sock = INVALID_SOCKET;
            }
            else
                break; // success
        }
    }
    freeaddrinfo(res0);
    if (sock == INVALID_SOCKET) {
        perror("socket");
        return -1;
    }

    //int const value_one = 1;
    //ret = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&value_one, sizeof(value_one));
    //if (ret < 0)
    //    fprintf(stderr, "rtl_tcp TCP_NODELAY failed\n");

    struct rtl_tcp_info info;
    ret = recv(sock, (char *)&info, sizeof (info), 0);
    if (ret != 12) {
        print_logf(LOG_ERROR, __func__, "Bad rtl_tcp header (%d)", ret);
        return -1;
    }
    if (strncmp(info.magic, "RTL0", 4)) {
        info.tuner_number = 0; // terminate magic
        print_logf(LOG_ERROR, __func__, "Bad rtl_tcp header magic \"%s\"", info.magic);
        return -1;
    }

    unsigned tuner_number = ntohl(info.tuner_number);
    //int tuner_gain_count  = ntohl(info.tuner_gain_count);

    char const *tuner_names[] = { "Unknown", "E4000", "FC0012", "FC0013", "FC2580", "R820T", "R828D" };
    char const *tuner_name = tuner_number > sizeof (tuner_names) ? "Invalid" : tuner_names[tuner_number];

    print_logf(LOG_CRITICAL, "SDR", "rtl_tcp connected to %s:%s (Tuner: %s)", host, port, tuner_name);

    sdr_dev_t *dev = calloc(1, sizeof(sdr_dev_t));
    if (!dev) {
        WARN_CALLOC("rtltcp_open()");
        return -1; // NOTE: returns error on alloc failure.
    }
#ifdef THREADS
    pthread_mutex_init(&dev->lock, NULL);
#endif

    dev->rtl_tcp = sock;
    dev->sample_size = sizeof(uint8_t) * 2; // CU8
    dev->sample_signed = 0;

    *out_dev = dev;
    return 0;
}

static int rtltcp_close(SOCKET sock)
{
    int ret = shutdown(sock, SHUT_RDWR);
    if (ret == -1) {
        perror("shutdown");
        return -1;
    }

    ret = closesocket(sock);
    if (ret == -1) {
        perror("close");
        return -1;
    }

    return 0;
}

static int rtltcp_read_loop(sdr_dev_t *dev, sdr_event_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len)
{
    size_t buffer_size = (size_t)buf_num * buf_len;
    if (dev->buffer_size != buffer_size) {
        free(dev->buffer);
        dev->buffer = malloc(buffer_size);
        if (!dev->buffer) {
            WARN_MALLOC("rtltcp_read_loop()");
            return -1; // NOTE: returns error on alloc failure.
        }
        dev->buffer_size = buffer_size;
        dev->buffer_pos = 0;
    }

    dev->running = 1;
    do {
        if (dev->buffer_pos + buf_len > buffer_size)
            dev->buffer_pos = 0;
        uint8_t *buffer = &dev->buffer[dev->buffer_pos];
        dev->buffer_pos += buf_len;

        unsigned n_read = 0;
        int r;

        do {
            r = recv(dev->rtl_tcp, &buffer[n_read], buf_len - n_read, MSG_WAITALL);
            if (r <= 0)
                break;
            n_read += r;
            //fprintf(stderr, "readStream ret=%d (of %u)\n", r, n_read);
        } while (n_read < buf_len);
        //fprintf(stderr, "readStream ret=%d (read %u)\n", r, n_read);

        if (r < 0) {
            print_logf(LOG_WARNING, __func__, "sync read failed. %d", r);
        }
        if (n_read == 0) {
            perror("rtl_tcp");
            dev->running = 0;
        }

#ifdef THREADS
        pthread_mutex_lock(&dev->lock);
#endif
        uint32_t sample_rate      = dev->sample_rate;
        uint32_t center_frequency = dev->center_frequency;
#ifdef THREADS
        pthread_mutex_unlock(&dev->lock);
#endif
        sdr_event_t ev = {
                .ev               = SDR_EV_DATA,
                .sample_rate      = sample_rate,
                .center_frequency = center_frequency,
                .buf              = buffer,
                .len              = n_read,
        };
#ifdef THREADS
        pthread_mutex_lock(&dev->lock);
        int exit_acquire = dev->exit_acquire;
        pthread_mutex_unlock(&dev->lock);
        if (exit_acquire) {
            break; // do not deliver any more events
        }
#endif
        if (n_read > 0) // prevent a crash in callback
            cb(&ev, ctx);

    } while (dev->running);

    return 0;
}

#pragma pack(push, 1)
struct command {
    unsigned char cmd;
    unsigned int param;
};
#pragma pack(pop)

// rtl_tcp API
#define RTLTCP_SET_FREQ 0x01
#define RTLTCP_SET_SAMPLE_RATE 0x02
#define RTLTCP_SET_GAIN_MODE 0x03
#define RTLTCP_SET_GAIN 0x04
#define RTLTCP_SET_FREQ_CORRECTION 0x05
#define RTLTCP_SET_IF_TUNER_GAIN 0x06
#define RTLTCP_SET_TEST_MODE 0x07
#define RTLTCP_SET_AGC_MODE 0x08
#define RTLTCP_SET_DIRECT_SAMPLING 0x09
#define RTLTCP_SET_OFFSET_TUNING 0x0a
#define RTLTCP_SET_RTL_XTAL 0x0b
#define RTLTCP_SET_TUNER_XTAL 0x0c
#define RTLTCP_SET_TUNER_GAIN_BY_ID 0x0d
#define RTLTCP_SET_BIAS_TEE 0x0e

static int rtltcp_command(sdr_dev_t *dev, char cmd, int param)
{
    struct command command;
    command.cmd   = cmd;
    command.param = htonl(param);

    return sizeof(command) == send(dev->rtl_tcp, (const char*) &command, sizeof(command), 0) ? 0 : -1;
}

/* RTL-SDR helpers */

#ifdef RTLSDR

static int sdr_open_rtl(sdr_dev_t **out_dev, char const *dev_query, int verbose)
{
    uint32_t device_count = rtlsdr_get_device_count();
    if (!device_count) {
        print_log(LOG_CRITICAL, "SDR", "No supported devices found.");
        return -1;
    }

    if (verbose)
        print_logf(LOG_NOTICE, "SDR", "Found %u device(s)", device_count);

    int dev_index = 0;
    // select rtlsdr device by serial (-d :<serial>)
    if (dev_query && *dev_query == ':') {
        dev_index = rtlsdr_get_index_by_serial(&dev_query[1]);
        if (dev_index < 0) {
            if (verbose)
                print_logf(LOG_ERROR, "SDR", "Could not find device with serial '%s' (err %d)",
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
    if (!dev) {
        WARN_CALLOC("sdr_open_rtl()");
        return -1; // NOTE: returns error on alloc failure.
    }
#ifdef THREADS
    pthread_mutex_init(&dev->lock, NULL);
#endif

    for (uint32_t i = dev_query ? dev_index : 0;
            //cast quiets -Wsign-compare; if dev_index were < 0, would have returned -1 above
            i < (dev_query ? (unsigned)dev_index + 1 : device_count);
            i++) {
        rtlsdr_get_device_usb_strings(i, vendor, product, serial);

        if (verbose)
            print_logf(LOG_NOTICE, "SDR", "trying device %u: %s, %s, SN: %s",
                    i, vendor, product, serial);

        r = rtlsdr_open(&dev->rtlsdr_dev, i);
        if (r < 0) {
            if (verbose)
                print_logf(LOG_ERROR, __func__, "Failed to open rtlsdr device #%u.", i);
        }
        else {
            if (verbose)
                print_logf(LOG_CRITICAL, "SDR", "Using device %u: %s, %s, SN: %s, \"%s\"",
                        i, vendor, product, serial, rtlsdr_get_device_name(i));
            dev->sample_size = sizeof(uint8_t) * 2; // CU8
            dev->sample_signed = 0;

            size_t info_len = 41 + strlen(vendor) + strlen(product) + strlen(serial);
            dev->dev_info = malloc(info_len);
            if (!dev->dev_info)
                FATAL_MALLOC("sdr_open_rtl");
            snprintf(dev->dev_info, info_len, "{\"vendor\":\"%s\", \"product\":\"%s\", \"serial\":\"%s\"}",
                    vendor, product, serial);
            break;
        }
    }
    if (r < 0) {
        free(dev);
        if (verbose)
            print_log(LOG_ERROR, __func__, "Unable to open a device");
    }
    else {
        *out_dev = dev;
    }
    return r;
}

static int rtlsdr_find_tuner_gain(sdr_dev_t *dev, int centigain, int verbose)
{
    /* Get allowed gains */
    int gains_count = rtlsdr_get_tuner_gains(dev->rtlsdr_dev, NULL);
    if (gains_count < 0) {
        if (verbose)
            print_log(LOG_WARNING, __func__, "Unable to get exact gains");
        return centigain;
    }
    if (gains_count < 1) {
        if (verbose)
            print_log(LOG_WARNING, __func__, "No exact gains");
        return centigain;
    }
    if (gains_count > 29) {
        print_log(LOG_ERROR, __func__, "Unexpected gain count, notify maintainers please!");
        return centigain;
    }
    // We known the maximum nunmber of gains is 29.
    // Let's not waste an alloc
    int gains[29] = {0};
    rtlsdr_get_tuner_gains(dev->rtlsdr_dev, gains);

    /* Find allowed gain */
    for (int i = 0; i < gains_count; ++i) {
        if (centigain <= gains[i]) {
            centigain = gains[i];
            break;
        }
    }
    if (centigain > gains[gains_count - 1]) {
        centigain = gains[gains_count - 1];
    }

    return centigain;
}

static void rtlsdr_read_cb(unsigned char *iq_buf, uint32_t len, void *ctx)
{
    sdr_dev_t *dev = ctx;

    //fprintf(stderr, "rtlsdr_read_cb enter...\n");
#ifdef THREADS
    pthread_mutex_lock(&dev->lock);
    int exit_acquire = dev->exit_acquire;
    pthread_mutex_unlock(&dev->lock);
    if (exit_acquire) {
        // we get one more call after rtlsdr_cancel_async(),
        // it then takes a full second until rtlsdr_read_async() ends.
        //fprintf(stderr, "rtlsdr_read_cb stopping...\n");
        return; // do not deliver any more events
    }
#endif

    if (dev->buffer_pos + len > dev->buffer_size)
        dev->buffer_pos = 0;
    uint8_t *buffer = &dev->buffer[dev->buffer_pos];
    dev->buffer_pos += len;

    // NOTE: we need to copy the buffer, it might go away on cancel_async
    memcpy(buffer, iq_buf, len);

#ifdef THREADS
    pthread_mutex_lock(&dev->lock);
#endif
    uint32_t sample_rate      = dev->sample_rate;
    uint32_t center_frequency = dev->center_frequency;
#ifdef THREADS
    pthread_mutex_unlock(&dev->lock);
#endif
    sdr_event_t ev = {
            .ev               = SDR_EV_DATA,
            .sample_rate      = sample_rate,
            .center_frequency = center_frequency,
            .buf              = buffer,
            .len              = len,
    };
    //fprintf(stderr, "rtlsdr_read_cb cb...\n");
    if (len > 0) // prevent a crash in callback
        dev->rtlsdr_cb(&ev, dev->rtlsdr_cb_ctx);
    //fprintf(stderr, "rtlsdr_read_cb cb done.\n");
    // NOTE: we actually need to copy the buffer to prevent it going away on cancel_async
}

static int rtlsdr_read_loop(sdr_dev_t *dev, sdr_event_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len)
{
    size_t buffer_size = (size_t)buf_num * buf_len;
    if (dev->buffer_size != buffer_size) {
        free(dev->buffer);
        dev->buffer = malloc(buffer_size);
        if (!dev->buffer) {
            WARN_MALLOC("rtlsdr_read_loop()");
            return -1; // NOTE: returns error on alloc failure.
        }
        dev->buffer_size = buffer_size;
        dev->buffer_pos = 0;
    }

    int r = 0;

    dev->rtlsdr_cb = cb;
    dev->rtlsdr_cb_ctx = ctx;

    dev->running = 1;

        r = rtlsdr_read_async(dev->rtlsdr_dev, rtlsdr_read_cb, dev, buf_num, buf_len);
        // rtlsdr_read_async() returns possible error codes from:
        //     if (!dev) return -1;
        //     if (RTLSDR_INACTIVE != dev->async_status) return -2;
        //     r = libusb_submit_transfer(dev->xfer[i]);
        //     r = libusb_handle_events_timeout_completed(dev->ctx, &tv,
        //     r = libusb_cancel_transfer(dev->xfer[i]);
        // We can safely assume it's an libusb error.
        if (r < 0) {
#ifdef LIBUSB1
            print_logf(LOG_ERROR, __func__, "%s: %s! "
                            "Check your RTL-SDR dongle, USB cables, and power supply.",
                    libusb_error_name(r), libusb_strerror(r));
#else
            print_logf(LOG_ERROR, __func__, "LIBUSB_ERROR: %d! "
                            "Check your RTL-SDR dongle, USB cables, and power supply.",
                    r);
#endif
            dev->running = 0;
        }
    print_log(LOG_DEBUG, __func__, "rtlsdr_read_async done");

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
        print_log(LOG_WARNING, "SDR", "Failed to set bandwidth.");
    }
    else if (bandwidth > 0) {
        applied_bw = (uint32_t)SoapySDRDevice_getBandwidth(dev, SOAPY_SDR_RX, 0);
        if (applied_bw)
            print_logf(LOG_NOTICE, "SDR", "Bandwidth parameter %u Hz resulted in %u Hz.", bandwidth, applied_bw);
        else
            print_logf(LOG_NOTICE, "SDR", "Set bandwidth parameter %u Hz.", bandwidth);
    }
    else {
        print_logf(LOG_NOTICE, "SDR", "Bandwidth set to automatic resulted in %u Hz.", applied_bw);
    }
    return r;
}

static int soapysdr_direct_sampling(SoapySDRDevice *dev, int on)
{
    int r = 0;
    char const *value;
    if (on == 0)
        value = "0";
    else if (on == 1)
        value = "1";
    else if (on == 2)
        value = "2";
    else
        return -1;
    SoapySDRDevice_writeSetting(dev, "direct_samp", value);
    char *set_value = SoapySDRDevice_readSetting(dev, "direct_samp");

    if (set_value == NULL) {
        print_log(LOG_ERROR, __func__, "Failed to set direct sampling moden");
        return r;
    }
    int set_num = atoi(set_value);
    if (set_num == 0) {
        print_log(LOG_CRITICAL, "SDR", "Direct sampling mode disabled.");}
    else if (set_num == 1) {
        print_log(LOG_CRITICAL, "SDR", "Enabled direct sampling mode, input 1/I.");}
    else if (set_num == 2) {
        print_log(LOG_CRITICAL, "SDR", "Enabled direct sampling mode, input 2/Q.");}
    else if (set_num == 3) {
        print_log(LOG_CRITICAL, "SDR", "Enabled no-mod direct sampling mode.");}
    SoapySDR_free(set_value);
    return r;
}

static int soapysdr_offset_tuning(SoapySDRDevice *dev)
{
    int r = 0;
    SoapySDRDevice_writeSetting(dev, "offset_tune", "true");
    char *set_value = SoapySDRDevice_readSetting(dev, "offset_tune");

    if (strcmp(set_value, "true") != 0) {
        /* TODO: detection of failure modes
        if (r == -2)
            print_log(LOG_WARNING, __func__, "Failed to set offset tuning: tuner doesn't support offset tuning!");
        else if (r == -3)
            print_log(LOG_WARNING, __func__, "Failed to set offset tuning: direct sampling not combinable with offset tuning!");
        else
        */
            print_log(LOG_WARNING, __func__, "Failed to set offset tuning.");
    }
    else {
        print_log(LOG_CRITICAL, "SDR", "Offset tuning mode enabled.");
    }
    SoapySDR_free(set_value);
    return r;
}

static int soapysdr_auto_gain(SoapySDRDevice *dev, int verbose)
{
    int r = 0;

    r = SoapySDRDevice_hasGainMode(dev, SOAPY_SDR_RX, 0);
    if (r) {
        r = SoapySDRDevice_setGainMode(dev, SOAPY_SDR_RX, 0, 1);
        if (r != 0) {
            print_log(LOG_WARNING, __func__, "Failed to enable automatic gain.");
        }
        else {
            if (verbose)
                print_log(LOG_CRITICAL, "SDR", "Tuner set to automatic gain.");
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
            print_log(LOG_WARNING, __func__, "Failed to set LNA tuner gain.");
        }
        r = SoapySDRDevice_setGainElement(dev, SOAPY_SDR_RX, 0, "VGA", 20.); // max 65
        if (r != 0) {
            print_log(LOG_WARNING, __func__, "Failed to set VGA tuner gain.");
        }
        r = SoapySDRDevice_setGainElement(dev, SOAPY_SDR_RX, 0, "AMP", 0.); // on or off
        if (r != 0) {
            print_log(LOG_WARNING, __func__, "Failed to set AMP tuner gain.");
        }

    }
    SoapySDR_free(driver);
    // otherwise leave unset, hopefully the driver has good defaults

    return r;
}

static int soapysdr_gain_str_set(SoapySDRDevice *dev, char const *gain_str, int verbose)
{
    if (!gain_str || !*gain_str || strlen(gain_str) >= GAIN_STR_MAX_SIZE)
        return -1;

    int r = 0;

    // Disable automatic gain
    r = SoapySDRDevice_hasGainMode(dev, SOAPY_SDR_RX, 0);
    if (r) {
        r = SoapySDRDevice_setGainMode(dev, SOAPY_SDR_RX, 0, 0);
        if (r != 0) {
            print_log(LOG_WARNING, __func__, "Failed to disable automatic gain.");
        }
        else {
            if (verbose)
                print_log(LOG_NOTICE, "SDR", "Tuner set to manual gain.");
        }
    }

    if (strchr(gain_str, '=')) {
        char gain_cpy[GAIN_STR_MAX_SIZE];
        strncpy(gain_cpy, gain_str, GAIN_STR_MAX_SIZE);
        gain_cpy[GAIN_STR_MAX_SIZE - 1] = '\0';
        char *gain_p = gain_cpy;
        // Set each gain individually (more control)
        char *name;
        char *value;
        while (getkwargs(&gain_p, &name, &value)) {
            double num = atof(value);
            if (verbose)
                print_logf(LOG_NOTICE, "SDR", "Setting gain element %s: %f dB", name, num);
            r = SoapySDRDevice_setGainElement(dev, SOAPY_SDR_RX, 0, name, num);
            if (r != 0) {
                print_logf(LOG_WARNING, __func__, "setGainElement(%s, %f) failed: %d", name, num, r);
            }
        }
    }
    else {
        // Set overall gain and let SoapySDR distribute amongst components
        double value = atof(gain_str);
        r = SoapySDRDevice_setGain(dev, SOAPY_SDR_RX, 0, value);
        if (r != 0) {
            print_log(LOG_WARNING, __func__, "Failed to set tuner gain.");
        }
        else {
            if (verbose)
                print_logf(LOG_NOTICE, __func__, "Tuner gain set to %0.2f dB.", value);
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
            SoapySDRStrings_clear(&gains, len);
        }
    }

    return r;
}

static void soapysdr_show_device_info(SoapySDRDevice *dev)
{
    size_t len = 0, i = 0;
    char *hwkey;
    SoapySDRKwargs args;
    char **antennas;
    char **gains;
    char **frequencies;
    SoapySDRRange *frequencyRanges;
    SoapySDRRange *rates;
    SoapySDRRange *bandwidths;
    double fullScale;
    char **stream_formats;
    char *native_stream_format;

    int direction = SOAPY_SDR_RX;
    size_t channel = 0;

    hwkey = SoapySDRDevice_getHardwareKey(dev);
    fprintf(stderr, "Using device %s: ", hwkey);
    SoapySDR_free(hwkey);

    args = SoapySDRDevice_getHardwareInfo(dev);
    for (i = 0; i < args.size; ++i) {
        fprintf(stderr, "%s=%s ", args.keys[i], args.vals[i]);
    }
    fprintf(stderr, "\n");
    SoapySDRKwargs_clear(&args);

    antennas = SoapySDRDevice_listAntennas(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu antenna(s): ", len);
    for (i = 0; i < len; ++i) {
        fprintf(stderr, "%s ", antennas[i]);
    }
    fprintf(stderr, "\n");
    SoapySDRStrings_clear(&antennas, len);

    gains = SoapySDRDevice_listGains(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu gain(s): ", len);
    for (i = 0; i < len; ++i) {
        SoapySDRRange gainRange = SoapySDRDevice_getGainRange(dev, direction, channel);
        fprintf(stderr, "%s %.0f - %.0f (step %.0f) ", gains[i], gainRange.minimum, gainRange.maximum, gainRange.step);
    }
    fprintf(stderr, "\n");
    SoapySDRStrings_clear(&gains, len);

    frequencies = SoapySDRDevice_listFrequencies(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu frequencies: ", len);
    for (i = 0; i < len; ++i) {
        fprintf(stderr, "%s ", frequencies[i]);
    }
    fprintf(stderr, "\n");
    SoapySDRStrings_clear(&frequencies, len);

    frequencyRanges = SoapySDRDevice_getFrequencyRange(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu frequency range(s): ", len);
    for (i = 0; i < len; ++i) {
        fprintf(stderr, "%.0f - %.0f (step %.0f) ", frequencyRanges[i].minimum, frequencyRanges[i].maximum, frequencyRanges[i].step);
    }
    fprintf(stderr, "\n");
    SoapySDR_free(frequencyRanges);

    rates = SoapySDRDevice_getSampleRateRange(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu sample rate range(s): ", len);
    for (i = 0; i < len; ++i) {
        if (rates[i].minimum == rates[i].maximum)
            fprintf(stderr, "%.0f ", rates[i].minimum);
        else
            fprintf(stderr, "%.0f - %.0f (step %.0f) ", rates[i].minimum, rates[i].maximum, rates[i].step);
    }
    fprintf(stderr, "\n");
    SoapySDR_free(rates);

    bandwidths = SoapySDRDevice_getBandwidthRange(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu bandwidth range(s): ", len);
    for (i = 0; i < len; ++i) {
        fprintf(stderr, "%.0f - %.0f (step %.0f) ", bandwidths[i].minimum, bandwidths[i].maximum, bandwidths[i].step);
    }
    fprintf(stderr, "\n");
    SoapySDR_free(bandwidths);

    double bandwidth = SoapySDRDevice_getBandwidth(dev, direction, channel);
    fprintf(stderr, "Found current bandwidth %.0f\n", bandwidth);

    stream_formats = SoapySDRDevice_getStreamFormats(dev, direction, channel, &len);
    fprintf(stderr, "Found %zu stream format(s): ", len);
    for (i = 0; i < len; ++i) {
        fprintf(stderr, "%s ", stream_formats[i]);
    }
    fprintf(stderr, "\n");
    SoapySDRStrings_clear(&stream_formats, len);

    native_stream_format = SoapySDRDevice_getNativeStreamFormat(dev, direction, channel, &fullScale);
    fprintf(stderr, "Found native stream format: %s (full scale: %.1f)\n", native_stream_format, fullScale);
    SoapySDR_free(native_stream_format);
}

static int sdr_open_soapy(sdr_dev_t **out_dev, char const *dev_query, int verbose)
{
    if (verbose)
        SoapySDR_setLogLevel(SOAPY_SDR_DEBUG);

    sdr_dev_t *dev = calloc(1, sizeof(sdr_dev_t));
    if (!dev) {
        WARN_CALLOC("sdr_open_soapy()");
        return -1; // NOTE: returns error on alloc failure.
    }
#ifdef THREADS
    pthread_mutex_init(&dev->lock, NULL);
#endif

    dev->soapy_dev = SoapySDRDevice_makeStrArgs(dev_query);
    if (!dev->soapy_dev) {
        if (verbose)
            print_logf(LOG_ERROR, __func__, "Failed to open sdr device matching '%s'.", dev_query);
        free(dev);
        return -1;
    }

    if (verbose)
        soapysdr_show_device_info(dev->soapy_dev);

    // select a stream format, in preference order: native CU8, CS8, CS16, forced CS16
    // stream_formats = SoapySDRDevice_getStreamFormats(dev->soapy_dev, SOAPY_SDR_RX, 0, &len);
    char *native_format = SoapySDRDevice_getNativeStreamFormat(dev->soapy_dev, SOAPY_SDR_RX, 0, &dev->fullScale);
    char const *selected_format;
    if (!strcmp(SOAPY_SDR_CU8, native_format)) {
        // actually not supported by SoapySDR
        selected_format = SOAPY_SDR_CU8;
        dev->sample_size = sizeof(uint8_t); // CU8
        dev->sample_signed = 0;
    }
//    else if (!strcmp(SOAPY_SDR_CS8, native_format)) {
//        // TODO: CS8 needs conversion to CU8
//        // e.g. RTL-SDR (8 bit), scale is 128.0
//        selected_format = SOAPY_SDR_CS8;
//        dev->sample_size = sizeof(int8_t) * 2; // CS8
//        dev->sample_signed = 1;
//    }
    else if (!strcmp(SOAPY_SDR_CS16, native_format)) {
        // e.g. LimeSDR-mini (12 bit), native scale is 2048.0
        // e.g. SDRplay RSP1A (14 bit), native scale is 32767.0
        selected_format = SOAPY_SDR_CS16;
        dev->sample_size = sizeof(int16_t) * 2; // CS16
        dev->sample_signed = 1;
    }
    else {
        // force CS16
        selected_format = SOAPY_SDR_CS16;
        dev->sample_size = sizeof(int16_t) * 2; // CS16
        dev->sample_signed = 1;
        dev->fullScale = 32768.0; // assume max for SOAPY_SDR_CS16
    }
    SoapySDR_free(native_format);

    SoapySDRKwargs args = SoapySDRDevice_getHardwareInfo(dev->soapy_dev);
    size_t info_len     = 2;
    for (size_t i = 0; i < args.size; ++i) {
        info_len += strlen(args.keys[i]) + strlen(args.vals[i]) + 6;
    }
    char *p = dev->dev_info = malloc(info_len);
    if (!dev->dev_info)
        FATAL_MALLOC("sdr_open_soapy");
    for (size_t i = 0; i < args.size; ++i) {
        p += sprintf(p, "%s\"%s\":\"%s\"", i ? "," : "{", args.keys[i], args.vals[i]);
    }
    sprintf(p, "}");
    SoapySDRKwargs_clear(&args);

    SoapySDRKwargs stream_args = {0};
    int r;
#if SOAPY_SDR_API_VERSION >= 0x00080000
    // API version 0.8
#undef SoapySDRDevice_setupStream
    dev->soapy_stream = SoapySDRDevice_setupStream(dev->soapy_dev, SOAPY_SDR_RX, selected_format, NULL, 0, &stream_args);
    r = dev->soapy_stream == NULL;
#else
    // API version 0.7
    r = SoapySDRDevice_setupStream(dev->soapy_dev, &dev->soapy_stream, SOAPY_SDR_RX, selected_format, NULL, 0, &stream_args);
#endif
    if (r != 0) {
        if (verbose)
            print_log(LOG_ERROR, __func__, "Failed to setup sdr device");
        free(dev);
        return -3;
    }

    *out_dev = dev;
    return 0;
}

static int soapysdr_read_loop(sdr_dev_t *dev, sdr_event_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len)
{
    size_t buffer_size = (size_t)buf_num * buf_len;
    if (dev->buffer_size != buffer_size) {
        free(dev->buffer);
        dev->buffer = malloc(buffer_size);
        if (!dev->buffer) {
            WARN_MALLOC("soapysdr_read_loop()");
            return -1; // NOTE: returns error on alloc failure.
        }
        dev->buffer_size = buffer_size;
        dev->buffer_pos = 0;
    }

    size_t buf_elems = buf_len / dev->sample_size;

    dev->running = 1;
    do {
        if (dev->buffer_pos + buf_len > buffer_size)
            dev->buffer_pos = 0;
        int16_t *buffer = (void *)&dev->buffer[dev->buffer_pos];
        dev->buffer_pos += buf_len;

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
        //fprintf(stderr, "readStream ret=%u (%u), flags=%d, timeNs=%lld\n", n_read, buf_len, flags, timeNs);
        if (r < 0) {
            if (r == SOAPY_SDR_OVERFLOW) {
                fprintf(stderr, "O");
                fflush(stderr);
                continue;
            }
            print_logf(LOG_WARNING, __func__, "sync read failed. %d", r);
        }

        // convert to CS16 or CU8 if needed
        // if converting CS8 to CU8 -- vectorized with -O3
        //for (i = 0; i < n_read * 2; ++i)
        //    cu8buf[i] = (int8_t)cu8buf[i] + 128;

        // TODO: SoapyRemote doesn't scale properly when reading (local) CS16 from (remote) CS8
        // rescale cs16 buffer
        if (dev->fullScale >= 2047.0 && dev->fullScale <= 2048.0) {
            for (i = 0; i < n_read * 2; ++i)
                buffer[i] *= 16; // prevent left shift of negative value
        }
        else if (dev->fullScale < 32767.0) {
            int upscale = 32768 / dev->fullScale;
            for (i = 0; i < n_read * 2; ++i)
                buffer[i] *= upscale;
        }

#ifdef THREADS
        pthread_mutex_lock(&dev->lock);
#endif
        uint32_t sample_rate      = dev->sample_rate;
        uint32_t center_frequency = dev->center_frequency;
#ifdef THREADS
        pthread_mutex_unlock(&dev->lock);
#endif
        sdr_event_t ev = {
                .ev               = SDR_EV_DATA,
                .sample_rate      = sample_rate,
                .center_frequency = center_frequency,
                .buf              = buffer,
                .len              = n_read * dev->sample_size,
        };
#ifdef THREADS
        pthread_mutex_lock(&dev->lock);
        int exit_acquire = dev->exit_acquire;
        pthread_mutex_unlock(&dev->lock);
        if (exit_acquire) {
            break; // do not deliver any more events
        }
#endif
        if (n_read > 0) // prevent a crash in callback
            cb(&ev, ctx);

    } while (dev->running);

    return 0;
}

#endif

/* Public API */

int sdr_open(sdr_dev_t **out_dev, char const *dev_query, int verbose)
{
    if (dev_query && !strncmp(dev_query, "rtl_tcp", 7))
        return rtltcp_open(out_dev, dev_query, verbose);

#if !defined(RTLSDR) && !defined(SOAPYSDR)
    if (verbose)
        print_log(LOG_ERROR, __func__, "No input drivers (RTL-SDR or SoapySDR) compiled in.");
    return -1;
#endif

    /* Open RTLSDR by default or if index or serial given, if available */
    if (!dev_query || *dev_query == ':' || (*dev_query >= '0' && *dev_query <= '9')) {
#ifdef RTLSDR
        return sdr_open_rtl(out_dev, dev_query, verbose);
#else
        print_log(LOG_ERROR, __func__, "No input driver for RTL-SDR compiled in.");
        return -1;
#endif
    }

#ifdef SOAPYSDR
    UNUSED(soapysdr_set_bandwidth);
    UNUSED(soapysdr_direct_sampling);
    UNUSED(soapysdr_offset_tuning);

    /* Open SoapySDR otherwise, if available */
    return sdr_open_soapy(out_dev, dev_query, verbose);
#endif
    print_log(LOG_ERROR, __func__, "No input driver for SoapySDR compiled in.");

    return -1;
}

int sdr_close(sdr_dev_t *dev)
{
    if (!dev)
        return -1;

    int ret = sdr_stop(dev);

    if (dev->rtl_tcp)
        ret = rtltcp_close(dev->rtl_tcp);

#ifdef SOAPYSDR
    if (dev->soapy_dev)
        ret = SoapySDRDevice_unmake(dev->soapy_dev);
#endif

#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        ret = rtlsdr_close(dev->rtlsdr_dev);
#endif

#ifdef THREADS
    pthread_mutex_destroy(&dev->lock);
#endif

    free(dev->dev_info);
    free(dev->buffer);
    free(dev);
    return ret;
}

char const *sdr_get_dev_info(sdr_dev_t *dev)
{
    if (!dev)
        return NULL;

    return dev->dev_info;
}

int sdr_get_sample_size(sdr_dev_t *dev)
{
    if (!dev)
        return 0;

    return dev->sample_size;
}

int sdr_get_sample_signed(sdr_dev_t *dev)
{
    if (!dev)
        return 0;

    return dev->sample_signed;
}

int sdr_set_center_freq(sdr_dev_t *dev, uint32_t freq, int verbose)
{
    if (!dev)
        return -1;

#ifdef THREADS
    if (pthread_equal(dev->thread, pthread_self())) {
        fprintf(stderr, "%s: must not be called from acquire callback!\n", __func__);
        return -1;
    }
#endif

    int r = -1;

    if (dev->rtl_tcp) {
        dev->rtl_tcp_freq = freq;
        r = rtltcp_command(dev, RTLTCP_SET_FREQ, freq);
    }

#ifdef SOAPYSDR
    SoapySDRKwargs args = {0};
    if (dev->soapy_dev) {
        r = SoapySDRDevice_setFrequency(dev->soapy_dev, SOAPY_SDR_RX, 0, (double)freq, &args);
    }
#endif

#ifdef RTLSDR
    if (dev->rtlsdr_dev) {
        r = rtlsdr_set_center_freq(dev->rtlsdr_dev, freq);
        print_logf(LOG_DEBUG, "SDR", "rtlsdr_set_center_freq %u = %d", freq, r);
    }
#endif

    if (verbose) {
        if (r < 0)
            print_log(LOG_WARNING, __func__, "Failed to set center freq.");
        else
            print_logf(LOG_NOTICE, "SDR", "Tuned to %s.", nice_freq(sdr_get_center_freq(dev)));
    }

#ifdef THREADS
    pthread_mutex_lock(&dev->lock);
#endif
    dev->center_frequency = freq;
#ifdef THREADS
    pthread_mutex_unlock(&dev->lock);
#endif

    return r;
}

uint32_t sdr_get_center_freq(sdr_dev_t *dev)
{
    if (!dev)
        return 0;

    if (dev->rtl_tcp)
        return dev->rtl_tcp_freq;

#ifdef SOAPYSDR
    if (dev->soapy_dev)
        return (uint32_t)SoapySDRDevice_getFrequency(dev->soapy_dev, SOAPY_SDR_RX, 0);
#endif

#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        return rtlsdr_get_center_freq(dev->rtlsdr_dev);
#endif

    return 0;
}

int sdr_set_freq_correction(sdr_dev_t *dev, int ppm, int verbose)
{
    if (!dev)
        return -1;

#ifdef THREADS
    if (pthread_equal(dev->thread, pthread_self())) {
        fprintf(stderr, "%s: must not be called from acquire callback!\n", __func__);
        return -1;
    }
#endif

    int r = -1;

    if (dev->rtl_tcp)
        r = rtltcp_command(dev, RTLTCP_SET_FREQ_CORRECTION, ppm);

#ifdef SOAPYSDR
    if (dev->soapy_dev)
        r = SoapySDRDevice_setFrequencyComponent(dev->soapy_dev, SOAPY_SDR_RX, 0, "CORR", (double)ppm, NULL);
#endif

#ifdef RTLSDR
    if (dev->rtlsdr_dev) {
        r = rtlsdr_set_freq_correction(dev->rtlsdr_dev, ppm);
        if (r == -2)
            r = 0; // -2 is not an error code
    }
#endif

    if (verbose) {
        if (r < 0)
            print_log(LOG_WARNING, __func__, "Failed to set frequency correction.");
        else
            print_logf(LOG_NOTICE, "SDR", "Frequency correction set to %d ppm.", ppm);
    }
    return r;
}

int sdr_set_auto_gain(sdr_dev_t *dev, int verbose)
{
    if (!dev)
        return -1;

#ifdef THREADS
    if (pthread_equal(dev->thread, pthread_self())) {
        fprintf(stderr, "%s: must not be called from acquire callback!\n", __func__);
        return -1;
    }
#endif

    int r = -1;

    if (dev->rtl_tcp)
        r = rtltcp_command(dev, RTLTCP_SET_GAIN_MODE, 0);

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
            print_log(LOG_WARNING, __func__, "Failed to enable automatic gain.");
        else
            print_log(LOG_NOTICE, "SDR", "Tuner gain set to Auto.");
    }
    return r;
}

int sdr_set_tuner_gain(sdr_dev_t *dev, char const *gain_str, int verbose)
{
    if (!dev)
        return -1;

#ifdef THREADS
    if (pthread_equal(dev->thread, pthread_self())) {
        fprintf(stderr, "%s: must not be called from acquire callback!\n", __func__);
        return -1;
    }
#endif

    int r = -1;

    if (!gain_str || !*gain_str) {
        /* Enable automatic gain */
        return sdr_set_auto_gain(dev, verbose);
    }

#ifdef SOAPYSDR
    /* Enable manual gain */
    if (dev->soapy_dev)
        return soapysdr_gain_str_set(dev->soapy_dev, gain_str, verbose);
#endif

    int gain = (int)(atof(gain_str) * 10); /* tenths of a dB */
    if (gain == 0) {
        /* Enable automatic gain */
        return sdr_set_auto_gain(dev, verbose);
    }

    if (dev->rtl_tcp) {
        return rtltcp_command(dev, RTLTCP_SET_GAIN_MODE, 1)
                || rtltcp_command(dev, RTLTCP_SET_GAIN, gain);
    }

#ifdef RTLSDR
    /* Enable manual gain */
    r = rtlsdr_set_tuner_gain_mode(dev->rtlsdr_dev, 1);
    if (verbose)
        if (r < 0)
            print_log(LOG_WARNING, __func__, "Failed to enable manual gain.");

    /* Set the tuner gain */
    gain = rtlsdr_find_tuner_gain(dev, gain, verbose);

    /* Fix for FitiPower FC0012: set gain to minimum before desired value */
    if (rtlsdr_get_tuner_type(dev->rtlsdr_dev) == RTLSDR_TUNER_FC0012) {
        int minGain = -99;
        minGain = rtlsdr_find_tuner_gain(dev, minGain, verbose);

        r = rtlsdr_set_tuner_gain(dev->rtlsdr_dev, minGain);
        if (verbose) {
            if (r < 0)
                print_log(LOG_WARNING, __func__, "Failed to set initial gain.");
            else
                print_logf(LOG_NOTICE, "SDR", "Set initial gain for FC0012 to %f dB.", minGain / 10.0);
        }
    }

    r = rtlsdr_set_tuner_gain(dev->rtlsdr_dev, gain);
    if (verbose) {
        if (r < 0)
            print_log(LOG_WARNING, __func__, "Failed to set tuner gain.");
        else
            print_logf(LOG_NOTICE, "SDR", "Tuner gain set to %f dB.", gain / 10.0);
    }
#endif

    return r;
}

int sdr_set_antenna(sdr_dev_t *dev, char const *antenna_str, int verbose)
{
    if (!dev)
        return -1;

    POSSIBLY_UNUSED(verbose);
    int r = -1;

    if (!antenna_str)
        return 0;

#ifdef SOAPYSDR
    if (dev->soapy_dev) {
        r = SoapySDRDevice_setAntenna(dev->soapy_dev, SOAPY_SDR_RX, 0, antenna_str);

        if (verbose) {
            if (r < 0)
                print_log(LOG_WARNING, __func__, "Failed to set antenna.");

            // report the antenna that is actually used
            char *antenna = SoapySDRDevice_getAntenna(dev->soapy_dev, SOAPY_SDR_RX, 0);
            print_logf(LOG_NOTICE, "SDR", "Antenna set to '%s'.", antenna);
            free(antenna);
        }
        return r;
    }
#endif

  // currently only SoapySDR supports devices with multiple antennas
  print_log(LOG_WARNING, __func__, "Antenna selection only available for SoapySDR devices");

  return r;
}

int sdr_set_sample_rate(sdr_dev_t *dev, uint32_t rate, int verbose)
{
    if (!dev)
        return -1;

#ifdef THREADS
    if (pthread_equal(dev->thread, pthread_self())) {
        fprintf(stderr, "%s: must not be called from acquire callback!\n", __func__);
        return -1;
    }
#endif

    int r = -1;

    if (dev->rtl_tcp) {
        dev->rtl_tcp_rate = rate;
        r = rtltcp_command(dev, RTLTCP_SET_SAMPLE_RATE, rate);
    }

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
            print_log(LOG_WARNING, __func__, "Failed to set sample rate.");
        else
            print_logf(LOG_NOTICE, "SDR", "Sample rate set to %u S/s.", sdr_get_sample_rate(dev)); // Unfortunately, doesn't return real rate
    }

#ifdef THREADS
    pthread_mutex_lock(&dev->lock);
#endif
    dev->sample_rate = rate;
#ifdef THREADS
    pthread_mutex_unlock(&dev->lock);
#endif

    return r;
}

uint32_t sdr_get_sample_rate(sdr_dev_t *dev)
{
    if (!dev)
        return 0;

    if (dev->rtl_tcp)
        return dev->rtl_tcp_rate;

#ifdef SOAPYSDR
    if (dev->soapy_dev)
        return (uint32_t)SoapySDRDevice_getSampleRate(dev->soapy_dev, SOAPY_SDR_RX, 0);
#endif

#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        return rtlsdr_get_sample_rate(dev->rtlsdr_dev);
#endif

    return 0;
}

int sdr_apply_settings(sdr_dev_t *dev, char const *sdr_settings, int verbose)
{
    if (!dev)
        return -1;

    POSSIBLY_UNUSED(verbose);
    int r = 0;

    if (!sdr_settings || !*sdr_settings)
        return 0;

    if (dev->rtl_tcp) {
        while (sdr_settings && *sdr_settings) {
            char const *val = NULL;
            // This mirrors the settings of SoapyRTLSDR
            if (kwargs_match(sdr_settings, "direct_samp", &val)) {
                int direct_sampling = atoiv(val, 1);
                r = rtltcp_command(dev, RTLTCP_SET_DIRECT_SAMPLING, direct_sampling);
            }
            else if (kwargs_match(sdr_settings, "offset_tune", &val)) {
                int offset_tuning = atobv(val, 1);
                r = rtltcp_command(dev, RTLTCP_SET_OFFSET_TUNING, offset_tuning);
            }
            else if (kwargs_match(sdr_settings, "digital_agc", &val)) {
                int digital_agc = atobv(val, 1);
                r = rtltcp_command(dev, RTLTCP_SET_AGC_MODE, digital_agc);
            }
            else if (kwargs_match(sdr_settings, "biastee", &val)) {
                int biastee = atobv(val, 1);
                r = rtltcp_command(dev, RTLTCP_SET_BIAS_TEE, biastee);
            }
            else {
                print_logf(LOG_ERROR, __func__, "Unknown rtl_tcp setting: %s", sdr_settings);
                return -1;
            }
            sdr_settings = kwargs_skip(sdr_settings);
        }
        return r;
    }

#ifdef SOAPYSDR
    if (dev->soapy_dev) {
        SoapySDRKwargs settings = SoapySDRKwargs_fromString(sdr_settings);
        for (size_t i = 0; i < settings.size; ++i) {
            const char *key   = settings.keys[i];
            const char *value = settings.vals[i];
            if (verbose)
                print_logf(LOG_NOTICE, "SDR", "Setting %s to %s", key, value);
            if (!strcmp(key, "antenna")) {
                if (SoapySDRDevice_setAntenna(dev->soapy_dev, SOAPY_SDR_RX, 0, value) != 0) {
                    r = -1;
                    print_logf(LOG_WARNING, __func__, "Antenna setting failed: %s", SoapySDRDevice_lastError());
                }
            }
            else if (!strcmp(key, "bandwidth")) {
                uint32_t f_value = atouint32_metric(value, "-t bandwidth= ");
                if (SoapySDRDevice_setBandwidth(dev->soapy_dev, SOAPY_SDR_RX, 0, (double)f_value) != 0) {
                    r = -1;
                    print_logf(LOG_WARNING, __func__, "Bandwidth setting failed: %s", SoapySDRDevice_lastError());
                }
            }
            else {
                if (SoapySDRDevice_writeSetting(dev->soapy_dev, key, value) != 0) {
                    r = -1;
                    print_logf(LOG_WARNING, __func__, "sdr setting failed: %s", SoapySDRDevice_lastError());
                }
            }
        }
        SoapySDRKwargs_clear(&settings);
        return r;
    }
#endif

#ifdef RTLSDR
    if (dev->rtlsdr_dev) {
        while (sdr_settings && *sdr_settings) {
            char const *val = NULL;
            // This mirrors the settings of SoapyRTLSDR
            if (kwargs_match(sdr_settings, "direct_samp", &val)) {
                int direct_sampling = atoiv(val, 1);
                r = rtlsdr_set_direct_sampling(dev->rtlsdr_dev, direct_sampling);
            }
            else if (kwargs_match(sdr_settings, "offset_tune", &val)) {
                int offset_tuning = atobv(val, 1);
                r = rtlsdr_set_offset_tuning(dev->rtlsdr_dev, offset_tuning);
            }
            else if (kwargs_match(sdr_settings, "digital_agc", &val)) {
                int digital_agc = atobv(val, 1);
                r = rtlsdr_set_agc_mode(dev->rtlsdr_dev, digital_agc);
            }
            else if (kwargs_match(sdr_settings, "biastee", &val)) {
#if defined(__linux__) && (defined(__GNUC__) || defined(__clang__))
                // check weak link for Linux with older rtlsdr
                if (!rtlsdr_set_bias_tee) {
                    print_log(LOG_ERROR, __func__, "This librtlsdr version does not support biastee setting");
                    return -1;
                }
#endif
                int biastee = atobv(val, 1);
                r = rtlsdr_set_bias_tee(dev->rtlsdr_dev, biastee);
            }
            else {
                print_logf(LOG_ERROR, __func__, "Unknown RTLSDR setting: %s", sdr_settings);
                return -1;
            }
            sdr_settings = kwargs_skip(sdr_settings);
        }
        return r;
    }
#endif

    print_log(LOG_WARNING, __func__, "sdr settings not available."); // no open device

    return -1;
}

int sdr_activate(sdr_dev_t *dev)
{
    if (!dev)
        return -1;

#ifdef SOAPYSDR
    if (dev->soapy_dev) {
        if (SoapySDRDevice_activateStream(dev->soapy_dev, dev->soapy_stream, 0, 0, 0) != 0) {
            print_log(LOG_ERROR, __func__, "Failed to activate stream");
            exit(1);
        }
    }
#endif

    return 0;
}

int sdr_deactivate(sdr_dev_t *dev)
{
    if (!dev)
        return -1;

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
    if (!dev)
        return -1;

    int r = 0;

#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        r = rtlsdr_reset_buffer(dev->rtlsdr_dev);
#endif

    if (verbose) {
        if (r < 0)
            print_log(LOG_WARNING, __func__, "Failed to reset buffers.");
    }
    return r;
}

int sdr_start_sync(sdr_dev_t *dev, sdr_event_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len)
{
    if (!dev)
        return -1;

    if (buf_num == 0)
        buf_num = SDR_DEFAULT_BUF_NUMBER;
    if (buf_len == 0)
        buf_len = SDR_DEFAULT_BUF_LENGTH;

    if (dev->rtl_tcp)
        return rtltcp_read_loop(dev, cb, ctx, buf_num, buf_len);

#ifdef SOAPYSDR
    if (dev->soapy_dev)
        return soapysdr_read_loop(dev, cb, ctx, buf_num, buf_len);
#endif

#ifdef RTLSDR
    if (dev->rtlsdr_dev)
        return rtlsdr_read_loop(dev, cb, ctx, buf_num, buf_len);
#endif

    return -1;
}

int sdr_stop_sync(sdr_dev_t *dev)
{
    if (!dev)
        return -1;

    if (dev->rtl_tcp) {
        dev->running = 0;
        return 0;
    }

#ifdef SOAPYSDR
    if (dev->soapy_dev) {
        dev->running = 0;
        return 0;
    }
#endif

#ifdef RTLSDR
    if (dev->rtlsdr_dev) {
        dev->running = 0;
        return rtlsdr_cancel_async(dev->rtlsdr_dev);
    }
#endif

    return -1;
}

#ifdef SOAPYSDR
static void soapysdr_log_handler(const SoapySDRLogLevel level, const char *message)
{
    // Our log levels are compatible with SoapySDR.
    print_log((log_level_t)level, "SoapySDR", message);
}
#endif

void sdr_redirect_logging(void)
{
#ifdef SOAPYSDR
    SoapySDR_registerLogHandler(soapysdr_log_handler);
#endif
}

/* threading */

#ifdef THREADS
static THREAD_RETURN THREAD_CALL acquire_thread(void *arg)
{
    sdr_dev_t *dev = arg;
    print_log(LOG_DEBUG, __func__, "acquire_thread enter...");

    int r = sdr_start_sync(dev, dev->async_cb, dev->async_ctx, dev->buf_num, dev->buf_len);
    // if (cfg->verbosity > 1)
    print_log(LOG_DEBUG, __func__, "acquire_thread async stop...");

    if (r < 0) {
        print_logf(LOG_ERROR, "SDR", "async read failed (%i).", r);
    }

//    sdr_event_t ev = {
//            .ev  = SDR_EV_QUIT,
//    };
//    dev->async_cb(&ev, dev->async_ctx);

    print_log(LOG_DEBUG, __func__, "acquire_thread done...");
    return (THREAD_RETURN)(intptr_t)r;
}

int sdr_start(sdr_dev_t *dev, sdr_event_cb_t async_cb, void *async_ctx, uint32_t buf_num, uint32_t buf_len)
{
    if (!dev)
        return -1;

    dev->async_cb = async_cb;
    dev->async_ctx = async_ctx;
    dev->buf_num = buf_num;
    dev->buf_len = buf_len;

#ifndef _WIN32
    // Block all signals from the worker thread
    sigset_t sigset;
    sigset_t oldset;
    sigfillset(&sigset);
    pthread_sigmask(SIG_SETMASK, &sigset, &oldset);
#endif
    int r = pthread_create(&dev->thread, NULL, acquire_thread, dev);
#ifndef _WIN32
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
#endif
    if (r) {
        fprintf(stderr, "%s: error in pthread_create, rc: %d\n", __func__, r);
    }
    return r;
}

int sdr_stop(sdr_dev_t *dev)
{
    if (!dev)
        return -1;

    if (pthread_equal(dev->thread, pthread_self())) {
        fprintf(stderr, "%s: must not be called from acquire callback!\n", __func__);
        return -1;
    }

    print_log(LOG_DEBUG, __func__, "EXITING...");
    pthread_mutex_lock(&dev->lock);
    if (dev->exit_acquire) {
        pthread_mutex_unlock(&dev->lock);
        print_log(LOG_DEBUG, __func__, "Already exiting.");
        return 0;
    }
    dev->exit_acquire = 1; // for rtl_tcp and SoapySDR
    sdr_stop_sync(dev); // for rtlsdr
    pthread_mutex_unlock(&dev->lock);

    print_log(LOG_DEBUG, __func__, "JOINING...");
    int r = pthread_join(dev->thread, NULL);
    if (r) {
        fprintf(stderr, "%s: error in pthread_join, rc: %d\n", __func__, r);
    }

    print_log(LOG_DEBUG, __func__, "EXITED.");
    return r;
}
#else
int sdr_start(sdr_dev_t *dev, sdr_event_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len)
{
    UNUSED(dev);
    return -1;
}
int sdr_stop(sdr_dev_t *dev)
{
    UNUSED(dev);
    return -1;
}
#endif
