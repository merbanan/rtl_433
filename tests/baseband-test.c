/*
 * Baseband Evaluation
 *
 * Functional and speed test for various baseband functions.
 *
 * Copyright (C) 2018 by Christian Zuckschwerdt <zany@triq.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
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

#include "baseband.h"

#define MEASURE(label, block)                                              \
    do {                                                                   \
        clock_t start = clock();                                           \
        block;                                                             \
        clock_t stop   = clock();                                          \
        double elapsed = (double)(stop - start) * 1000.0 / CLOCKS_PER_SEC; \
        printf("Time elapsed in ms: %f for: %s\n", elapsed, label);        \
    } while (0);

int read_buf(const char *filename, void *buf, size_t nbyte)
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

int write_buf(const char *filename, const void *buf, size_t nbyte)
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

int main(int argc, char *argv[])
{
    baseband_init();

    uint8_t *cu8_buf;
    uint16_t *y16_buf;
    int16_t *cs16_buf;
    uint32_t *y32_buf;
    uint16_t *u16_buf;
    uint32_t *u32_buf;
    int16_t *s16_buf;
    int32_t *s32_buf;
    char *filename;
    long n_read;
    unsigned long n_samples;
    int max_block_size = 4096000;
    filter_state_t state;
    demodfm_state_t fm_state;

    if (argc <= 1) {
        return 1;
    }
    filename = argv[1];

    cu8_buf  = malloc(sizeof(uint8_t) * 2 * max_block_size);
    y16_buf  = malloc(sizeof(uint16_t) * max_block_size);
    cs16_buf = malloc(sizeof(int16_t) * 2 * max_block_size);
    y32_buf  = malloc(sizeof(uint32_t) * max_block_size);
    u16_buf  = malloc(sizeof(uint16_t) * max_block_size);
    u32_buf  = malloc(sizeof(uint32_t) * max_block_size);
    s16_buf  = malloc(sizeof(int16_t) * max_block_size);
    s32_buf  = malloc(sizeof(int32_t) * max_block_size);

    n_read = read_buf(filename, cu8_buf, sizeof(uint8_t) * 2 * max_block_size);
    if (n_read < 1) {
        return 1;
    }
    n_samples = n_read / (sizeof(uint8_t) * 2);

    for (unsigned long i = 0; i < n_samples * 2; i++) {
        //cs16_buf[i] = 127 - cu8_buf[i];
        //cs16_buf[i] = (int16_t)cu8_buf[i] * 16 - 2040;
        cs16_buf[i] = (int16_t)cu8_buf[i] * 128 - 16320;
        //cs16_buf[i] = (int16_t)cu8_buf[i] * 256 - 32640;
    }

    MEASURE("envelope_detect",
        envelope_detect(cu8_buf, y16_buf, n_samples);
    );
    MEASURE("envelope_detect_nolut",
        envelope_detect_nolut(cu8_buf, y16_buf, n_samples);
    );
    MEASURE("magnitude_est_cu8",
        magnitude_est_cu8(cu8_buf, y16_buf, n_samples);
    );
    MEASURE("magnitude_true_cu8",
        magnitude_true_cu8(cu8_buf, y16_buf, n_samples);
    );
    write_buf("bb.am.s16", y16_buf, sizeof(uint16_t) * n_samples);
    MEASURE("baseband_low_pass_filter",
        baseband_low_pass_filter(y16_buf, (int16_t *)u16_buf, n_samples, &state);
    );
    write_buf("bb.lp.am.s16", u16_buf, sizeof(int16_t) * n_samples);
    MEASURE("baseband_demod_FM",
        baseband_demod_FM(cu8_buf, s16_buf, n_samples, &fm_state);
    );
    write_buf("bb.fm.s16", s16_buf, sizeof(int16_t) * n_samples);

    write_buf("bb.cs16", cs16_buf, sizeof(int16_t) * 2 * n_samples);
    //envelope_detect_cs16(cs16_buf, y32_buf, n_samples);
    //write_buf("bb.am.u32", y32_buf, sizeof(uint32_t) * n_samples);
    //baseband_low_pass_filter_u32(y32_buf, u32_buf, n_samples, &state);
    //write_buf("bb.lp.am.u32", u32_buf, sizeof(uint32_t) * n_samples);

    MEASURE("magnitude_est_cs16",
        magnitude_est_cs16(cs16_buf, y16_buf, n_samples);
    );
    MEASURE("magnitude_true_cs16",
        magnitude_true_cs16(cs16_buf, y16_buf, n_samples);
    );
    write_buf("bb.mag.s16", y16_buf, sizeof(uint16_t) * n_samples);
    MEASURE("baseband_low_pass_filter",
        baseband_low_pass_filter(y16_buf, (int16_t *)u16_buf, n_samples, &state);
    );
    write_buf("bb.mag.lp.s16", u16_buf, sizeof(int16_t) * n_samples);

    //baseband_demod_FM_cs16(cs16_buf, s32_buf, n_samples, &fm_state);
    //write_buf("bb.fm.s32", s32_buf, sizeof(int32_t) * n_samples);

    MEASURE("baseband_demod_FM_cs16",
        baseband_demod_FM_cs16(cs16_buf, s16_buf, n_samples, &fm_state);
    );
    write_buf("bb.cs16.fm.s16", s16_buf, sizeof(int16_t) * n_samples);
}
