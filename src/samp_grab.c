/**
 * IQ sample grabber (ring buffer and dumper)
 *
 * Copyright (C) 2018 Christian Zuckschwerdt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#ifdef _MSC_VER
#define F_OK 0
#endif
#endif
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "samp_grab.h"

samp_grab_t *samp_grab_create(unsigned size)
{
    samp_grab_t *g;
    g = calloc(1, sizeof(*g));
    if (!g) {
        return NULL;
    }

    g->sg_buf  = malloc(size);
    g->sg_size = size;

    g->sg_counter = 1;

    if (!g->sg_buf) {
        free(g);
        return NULL;
    }

    return g;
}

void samp_grab_free(samp_grab_t *g)
{
    if (g->sg_buf)
        free(g->sg_buf);
    free(g);
}

void samp_grab_push(samp_grab_t *g, unsigned char *iq_buf, uint32_t len)
{
    //fprintf(stderr, "[%d] sg_index - len %d\n", g->sg_index, len );
    memcpy(&g->sg_buf[g->sg_index], iq_buf, len);
    g->sg_len = len;
    g->sg_index += len;
    if (g->sg_index + len > g->sg_size)
        g->sg_index = 0;
}

void samp_grab_reset(samp_grab_t *g)
{
    g->sg_len = 0;
    g->sg_index = 0;
}

void samp_grab_write(samp_grab_t *g, unsigned signal_start, unsigned signal_end, unsigned i)
{
    if (!g->sg_buf)
        return;

    int start_pos, signal_bsize, wlen, wrest = 0, sg_idx, idx;
    char f_name[64] = {0};
    FILE *fp;

    char *format = *g->sample_size == 1 ? "cu8" : "cs16";
    double freq_mhz = *g->frequency / 1000000.0;
    double rate_khz = *g->samp_rate / 1000.0;
    while (1) {
        sprintf(f_name, "g%03d_%gM_%gk.%s", g->sg_counter, freq_mhz, rate_khz, format);
        g->sg_counter++;
        if (access(f_name, F_OK) == -1) {
            break;
        }
    }

    signal_bsize = 2 * (signal_end - (signal_start - 10000));
    signal_bsize = (131072 - (signal_bsize % 131072)) + signal_bsize;
    sg_idx = g->sg_index - g->sg_len;
    if (sg_idx < 0)
        sg_idx = g->sg_size - g->sg_len;
    idx = (i - 40000) * 2;
    start_pos = sg_idx + idx - signal_bsize;
    fprintf(stderr, "signal_bsize = %d  -      sg_index = %d\n", signal_bsize, g->sg_index);
    fprintf(stderr, "start_pos    = %d  -   buffer_size = %d\n", start_pos, g->sg_size);
    if (signal_bsize > (int)g->sg_size)
        fprintf(stderr, "Signal bigger then buffer, signal = %d > buffer %d !!\n", signal_bsize, g->sg_size);

    if (start_pos < 0) {
        start_pos = g->sg_size + start_pos;
        fprintf(stderr, "restart_pos = %d\n", start_pos);
    }

    fprintf(stderr, "*** Saving signal to file %s\n", f_name);
    fp = fopen(f_name, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s\n", f_name);
    }
    wlen = signal_bsize;
    if (start_pos + signal_bsize > (int)g->sg_size) {
        wlen  = g->sg_size - start_pos;
        wrest = signal_bsize - wlen;
    }
    fprintf(stderr, "*** Writing data from %d, len %d\n", start_pos, wlen);
    fwrite(&g->sg_buf[start_pos], 1, wlen, fp);

    if (wrest) {
        fprintf(stderr, "*** Writing data from %d, len %d\n", 0, wrest);
        fwrite(&g->sg_buf[0], 1, wrest, fp);
    }

    fclose(fp);
}
