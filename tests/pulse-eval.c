/** @file
    Pulse Evaluation.

    Functional and speed test for various pulse functions.

    Copyright (C) 2018 by Christian Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

// gcc -Wall -I ../include -o pulse-eval ../src/baseband.c ../src/write_sigrok.c ../tests/pulse-eval.c && ./pulse-eval FILE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#ifdef _MSC_VER
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#ifdef _MSC_VER
#define F_OK 0
#define R_OK (1 << 2)
#endif
#endif
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include <time.h>
#include <assert.h>

#include "baseband.h"
#include "write_sigrok.h"

static int read_buf(char const *filename, void *buf, size_t nbyte)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s\n", filename);
        return -1;
    }
    ssize_t ret = read(fd, buf, nbyte);
    close(fd);
    return ret;
}

static int write_buf(char const *filename, void const *buf, size_t nbyte)
{
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s\n", filename);
        return -1;
    }
    ssize_t ret = write(fd, buf, nbyte);
    close(fd);
    return ret;
}

static int write_u16_to_f32(char const *filename, uint16_t const *u16, size_t len)
{
    float *f32 = malloc(sizeof(float) * len);
    assert(f32);
    for (size_t i = 0; i < len; ++i) {
        f32[i] = u16[i] / 65536.0;
    }
    int ret = write_buf(filename, f32, sizeof(float) * len);
    free(f32);
    return ret;
}

static int write_s16_to_f32(char const *filename, uint16_t const *s16, size_t len)
{
    float *f32 = malloc(sizeof(float) * len);
    assert(f32);
    for (size_t i = 0; i < len; ++i) {
        f32[i] = s16[i] / 32768.0;
    }
    int ret = write_buf(filename, f32, sizeof(float) * len);
    free(f32);
    return ret;
}

// ---

#define MAVG_WIDTH 8
typedef struct mavg {
    int idx;
    int avg;
    int vs[MAVG_WIDTH];       // values
} mavg_t;

static void mavg_push(mavg_t *a, int val)
{
    a->avg          = a->avg - a->vs[a->idx] + val;
    a->vs[a->idx++] = val;
    a->idx          = a->idx % MAVG_WIDTH;
}

static int mavg_avg(mavg_t *a)
{
    return a->avg / MAVG_WIDTH;
}

// ---

#define MAVGW_WIDTH 512
typedef struct mavgw {
    int idx;
    int avg;
    int vs[MAVGW_WIDTH]; // values
} mavgw_t;

static void mavgw_push(mavgw_t *a, int val)
{
    a->avg          = a->avg - a->vs[a->idx] + val;
    a->vs[a->idx++] = val;
    a->idx          = a->idx % MAVGW_WIDTH;
}

static int mavgw_avg(mavgw_t *a)
{
    return a->avg / MAVGW_WIDTH;
}

// ---

#define MAVGDEV_WIDTH 8
typedef struct mavgdev {
    int idx;
    int avg;
    unsigned dev;
    int vs[MAVGDEV_WIDTH]; // values
    unsigned msq[MAVGDEV_WIDTH]; // mean squares
} mavgdev_t;

static void mavgdev_push(mavgdev_t *a, int val)
{
    a->avg        = a->avg - a->vs[a->idx] + val;
    a->vs[a->idx] = val;

    int valc         = val - a->avg / MAVGDEV_WIDTH;
    //unsigned val2    = abs(valc);
    unsigned val2    = (valc * valc) >> 15;
    a->dev           = a->dev - a->msq[a->idx] + val2;
    a->msq[a->idx++] = val2;

    a->idx = a->idx % MAVGDEV_WIDTH;
}

static int mavgdev_avg(mavgdev_t *a)
{
    return a->avg / MAVGDEV_WIDTH;
}

static int mavgdev_dev(mavgdev_t *a)
{
    return a->dev / MAVGDEV_WIDTH;
}

// ---

int main(int argc, char *argv[])
{
    //baseband_init();

    char *filename;
    long n_read;
    size_t n_samples;
    int block_size = 4096000;
    unsigned sample_rate = 250000;

    int argi = 1;
    for (; argi < argc; ++argi) {
        if (*argv[argi] != '-')
            break;
        if (argv[argi][1] == 'b')
            block_size = atoi(argv[++argi]);
        else if (argv[argi][1] == 's')
            sample_rate = atoi(argv[++argi]);
        else {
            fprintf(stderr, "Wrong argument (%s).\n", argv[argi]);
            return 1;
        }
    }
    if (argc <= argi) {
        fprintf(stderr, "%s [-s samplerate] [-b blocksize] file", argv[0]);
        return 1;
    }
    filename = argv[argi];

    uint8_t *cu8_buf = malloc(sizeof(uint8_t) * 2 * block_size);
    assert(cu8_buf);
    uint8_t *cs8_buf = malloc(sizeof(uint8_t) * 2 * block_size);
    assert(cs8_buf);
    uint16_t *y16_buf = malloc(sizeof(uint16_t) * block_size);
    assert(y16_buf);
    uint16_t *am16_buf = malloc(sizeof(uint16_t) * block_size);
    assert(am16_buf);
    int16_t *fm16_buf = malloc(sizeof(int16_t) * block_size);
    assert(fm16_buf);
    uint8_t *u8_buf = calloc(block_size, sizeof(uint8_t));
    assert(u8_buf);

    uint16_t *mavgl = calloc(block_size, sizeof(uint16_t));
    assert(mavgl);
    uint16_t *mavgr = calloc(block_size, sizeof(uint16_t));
    assert(mavgr);
    uint16_t *mavgw = calloc(block_size, sizeof(uint16_t));
    assert(mavgw);

    uint16_t *mdevl = calloc(block_size, sizeof(uint16_t));
    assert(mdevl);
    uint16_t *mdevr = calloc(block_size, sizeof(uint16_t));
    assert(mdevr);

    uint16_t *davgl = calloc(block_size, sizeof(uint16_t));
    assert(davgl);
    uint16_t *davgr = calloc(block_size, sizeof(uint16_t));
    assert(davgr);

    n_read = read_buf(filename, cu8_buf, sizeof(uint8_t) * 2 * block_size);
    if (n_read < 1) {
        ret = 1;
        goto out;
    }
    n_samples = n_read / (sizeof(uint8_t) * 2);

    for (size_t i = 0; i < n_samples * 2; ++i) {
        cs8_buf[i] = cu8_buf[i] - 128;
    }

    magnitude_est_cu8(cu8_buf, y16_buf, n_samples);
    envelope_detect_nolut(cu8_buf, am16_buf, n_samples);
    demodfm_state_t fm_state;
    baseband_demod_FM(cu8_buf, fm16_buf, n_samples, &fm_state, 0);
    // envelope_detect(cu8_buf, y16_buf, n_samples);
    // magnitude_est_cu8(cu8_buf, y16_buf, n_samples);
    // baseband_low_pass_filter(y16_buf, (int16_t *)u16_buf, n_samples, &state);
    // baseband_demod_FM(cu8_buf, s16_buf, n_samples, &fm_state);

    // moving avgs (AM)
    mavg_t ml = {0};
    for (size_t i = 0; i < n_samples; ++i) {
        mavgl[i] = mavg_avg(&ml);
        mavg_push(&ml, y16_buf[i]);
    }

    mavg_t mr = {0};
    for (size_t i = 0; i < n_samples - 8; ++i) {
        mavgr[i] = mavg_avg(&mr);
        mavg_push(&mr, y16_buf[i + 8]);
    }

    mavgw_t mw = {0};
    for (size_t i = 0; i < n_samples; ++i) {
        mavgw[i] = mavgw_avg(&mw);
        mavgw_push(&mw, y16_buf[i]);
    }

    // slice mavgl by mavgw
    for (size_t i = 0; i < n_samples; ++i) {
        u8_buf[i] = mavgw[i] < 1000 ? 0 : mavgl[i] > mavgw[i] ? 0xff : 0;
    }

    // moving devs (FM)
    mavgdev_t vl = {0};
    for (size_t i = 0; i < n_samples; ++i) {
        mdevl[i] = mavgdev_dev(&vl);
        mavgdev_push(&vl, fm16_buf[i]);
    }

    mavgdev_t vr = {0};
    for (size_t i = 0; i < n_samples - 8; ++i) {
        mdevr[i] = mavgdev_dev(&vr);
        mavgdev_push(&vr, fm16_buf[i + 8]);
    }

    // decaying avgs
    int dl = y16_buf[0];
    for (size_t i = 0; i < n_samples; ++i) {
        davgl[i] = dl;
        dl = (dl + y16_buf[i]) / 2;
    }

    int dr = y16_buf[0];
    for (size_t i = 0; i < n_samples - 7; ++i) {
        davgr[i] = dr / 128;
        dr = 2 * dr - y16_buf[i] * 128 + y16_buf[i + 7];
    }

    // tests
    for (size_t i = 0; i < n_samples; ++i) {
        y16_buf[i] -= mdevr[i] / 16;
    }

    write_buf("logic-1-1", u8_buf, n_samples);
    write_s16_to_f32("analog-1-2-1", am16_buf, n_samples);
    write_s16_to_f32("analog-1-3-1", y16_buf, n_samples);
    write_s16_to_f32("analog-1-4-1", (uint16_t *)fm16_buf, n_samples);
    write_s16_to_f32("analog-1-5-1", mavgl, n_samples);
    write_s16_to_f32("analog-1-6-1", mavgr, n_samples);
    write_s16_to_f32("analog-1-7-1", mavgw, n_samples);
    write_s16_to_f32("analog-1-8-1", mdevl, n_samples);
    write_s16_to_f32("analog-1-9-1", mdevr, n_samples);
    write_s16_to_f32("analog-1-10-1", davgl, n_samples);
    write_s16_to_f32("analog-1-11-1", davgr, n_samples);

    char const *labels[] = {
            "logic",
            "am16",
            "y16",
            "fm16",
            "mavgl",
            "mavgr",
            "mavgw",
            "mdevl",
            "mdevr",
            "davgl",
            "davgr",
    };
    write_sigrok("out.sr", sample_rate, 1, 9, labels);
    open_pulseview("out.sr");

out:
    free(cu8_buf);
    free(cs8_buf);
    free(y16_buf);
    free(am16_buf);
    free(fm16_buf);
    free(u8_buf);
    free(mavgl);
    free(mavgr);
    free(mavgw);
    free(mdevl);
    free(mdevr);
    free(davgl);
    free(davgr);
}
