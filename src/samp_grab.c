/** @file
    IQ sample grabber (ring buffer and dumper).

    Copyright (C) 2018 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "samp_grab.h"

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

#include "sigmf.h"
#include "fatal.h"

samp_grab_t *samp_grab_create(unsigned size, int fileformat)
{
    samp_grab_t *g;
    g = calloc(1, sizeof(*g));
    if (!g) {
        WARN_CALLOC("samp_grab_create()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    g->sg_fileformat = fileformat;
    g->sg_size = size;
    g->sg_counter = 1;

    g->sg_buf = malloc(size);
    if (!g->sg_buf) {
        WARN_MALLOC("samp_grab_create()");
        free(g);
        return NULL; // NOTE: returns NULL on alloc failure.
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
    //fprintf(stderr, "sg_index %d + len %d (size %d ", g->sg_index, len, g->sg_len);

    g->sg_len += len;
    if (g->sg_len > g->sg_size)
        g->sg_len = g->sg_size;

    //fprintf(stderr, "-> %d)\n", g->sg_len);

    while (len) {
        unsigned chunk_len = len;
        if (g->sg_index + chunk_len > g->sg_size)
            chunk_len = g->sg_size - g->sg_index;

        memcpy(&g->sg_buf[g->sg_index], iq_buf, chunk_len);
        iq_buf += chunk_len;
        len -= chunk_len;
        g->sg_index += chunk_len;
        if (g->sg_index >= g->sg_size)
            g->sg_index = 0;
    }
}

void samp_grab_reset(samp_grab_t *g)
{
    g->sg_len = 0;
    g->sg_index = 0;
}

#define BLOCK_SIZE (128 * 1024) /* bytes */

void samp_grab_write(samp_grab_t *g, unsigned grab_len, unsigned grab_end)
{
    if (!g->sg_buf)
        return;

    double freq_mhz = *g->frequency / 1000000.0;
    double rate_khz = *g->samp_rate / 1000.0;

    unsigned signal_bsize = *g->sample_size * grab_len;
    signal_bsize += BLOCK_SIZE - (signal_bsize % BLOCK_SIZE);

    if (signal_bsize > g->sg_len) {
        fprintf(stderr, "Signal bigger than buffer, signal = %u > buffer %u !!\n", signal_bsize, g->sg_len);
        signal_bsize = g->sg_len;
    }

    // relative end in bytes from current sg_index down
    unsigned end_pos = *g->sample_size * grab_end;
    if (g->sg_index >= end_pos)
        end_pos = g->sg_index - end_pos;
    else
        end_pos = g->sg_size - end_pos + g->sg_index;
    // end_pos is now absolute in sg_buf

    unsigned start_pos;
    if (end_pos >= signal_bsize)
        start_pos = end_pos - signal_bsize;
    else
        start_pos = g->sg_size - signal_bsize + end_pos;

    //fprintf(stderr, "signal_bsize = %d  -      sg_index = %d\n", signal_bsize, g->sg_index);
    //fprintf(stderr, "start_pos    = %d  -   buffer_size = %d\n", start_pos, g->sg_size);

    unsigned wlen  = signal_bsize;
    unsigned wrest = 0;
    if (start_pos + signal_bsize > g->sg_size) {
        wlen  = g->sg_size - start_pos;
        wrest = signal_bsize - wlen;
    }

    if (!g->sg_fileformat) {
    char *datatype = *g->sample_size == 2 ? "cu8" : "cs16";

    char f_name[64] = {0};
    while (1) {
        snprintf(f_name, sizeof(f_name), "g%03u_%gM_%gk.%s", g->sg_counter, freq_mhz, rate_khz, datatype);
        g->sg_counter++;
        if (access(f_name, F_OK) == -1) {
            break;
        }
    }

    fprintf(stderr, "*** Saving signal to file %s (%u samples, %u bytes)\n", f_name, grab_len, signal_bsize);
    FILE *fp = fopen(f_name, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s\n", f_name);
        return;
    }
    //fprintf(stderr, "*** Writing data from %d, len %d\n", start_pos, wlen);
    fwrite(&g->sg_buf[start_pos], 1, wlen, fp);

    if (wrest) {
        //fprintf(stderr, "*** Writing data from %d, len %d\n", 0, wrest);
        fwrite(&g->sg_buf[0], 1, wrest, fp);
    }

    fclose(fp);
    }

    if (g->sg_fileformat) {
    char *datatype = *g->sample_size == 2 ? "cu8" : "ci16_le";

    char f_name[64] = {0};
    while (1) {
        snprintf(f_name, sizeof(f_name), "g%03u_%gM_%gk.%s", g->sg_counter, freq_mhz, rate_khz, "sigmf");
        g->sg_counter++;
        if (access(f_name, F_OK) == -1) {
            break;
        }
    }
    fprintf(stderr, "*** Saving signal to file %s (%u samples, %u bytes)\n", f_name, grab_len, signal_bsize);

    sigmf_t sigmf = {
            .datatype           = strdup(datatype),
            .sample_rate        = *g->samp_rate,
            .recorder           = strdup("rtl_433"),
            .description        = strdup("Sample grabbed by rtl_433"),
            .first_sample_start = 0,
            .first_frequency    = *g->frequency,
            .data_len           = signal_bsize,
    };

    int r = sigmf_writer_open(&sigmf, f_name, 0);
    if (r) {
        fprintf(stderr, "Failed to open %s\n", f_name);
        return;
    }

    // fprintf(stderr, "*** Writing data from %d, len %d\n", start_pos, wlen);
    fwrite(&g->sg_buf[start_pos], 1, wlen, sigmf.mtar.stream);

    if (wrest) {
        // fprintf(stderr, "*** Writing data from %d, len %d\n", 0, wrest);
        fwrite(&g->sg_buf[0], 1, wrest, sigmf.mtar.stream);
    }

    r = sigmf_writer_close(&sigmf);
    if (r) {
        fprintf(stderr, "Failed to close %s\n", f_name);
        return;
    }

    r = sigmf_free_items(&sigmf);
    if (r) {
        fprintf(stderr, "Failed to free SigMF writer %s\n", f_name);
        return;
    }
    }
}
