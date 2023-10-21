/** @file
    Pulse data functions.

    Copyright (C) 2015 Tommy Vestermark
    Copyright (C) 2022 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "pulse_data.h"
#include "rfraw.h"
#include "r_util.h"
#include "fatal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pulse_data_clear(pulse_data_t *data)
{
    *data = (pulse_data_t const){0};
}

void pulse_data_shift(pulse_data_t *data)
{
    int offs = PD_MAX_PULSES / 2; // shift out half the data
    memmove(data->pulse, &data->pulse[offs], (PD_MAX_PULSES - offs) * sizeof(*data->pulse));
    memmove(data->gap, &data->gap[offs], (PD_MAX_PULSES - offs) * sizeof(*data->gap));
    data->num_pulses -= offs;
    data->offset += offs;
}

void pulse_data_print(pulse_data_t const *data)
{
    fprintf(stderr, "Pulse data: %u pulses\n", data->num_pulses);
    for (unsigned n = 0; n < data->num_pulses; ++n) {
        fprintf(stderr, "[%3u] Pulse: %4d, Gap: %4d, Period: %4d\n", n, data->pulse[n], data->gap[n], data->pulse[n] + data->gap[n]);
    }
}

static void *bounded_memset(void *b, int c, int64_t size, int64_t offset, int64_t len)
{
    if (offset < 0) {
        len += offset; // reduce len by negative offset
        offset = 0;
    }
    if (offset + len > size) {
        len = size - offset; // clip excessive len
    }
    if (len > 0)
        memset((char *)b + offset, c, (size_t)len);
    return b;
}

void pulse_data_dump_raw(uint8_t *buf, unsigned len, uint64_t buf_offset, pulse_data_t const *data, uint8_t bits)
{
    int64_t pos = data->offset - buf_offset;
    for (unsigned n = 0; n < data->num_pulses; ++n) {
        bounded_memset(buf, 0x01 | bits, len, pos, data->pulse[n]);
        pos += data->pulse[n];
        bounded_memset(buf, 0x01, len, pos, data->gap[n]);
        pos += data->gap[n];
    }
}

static inline void chk_ret(int ret)
{
    if (ret < 0) {
        perror("File output error");
        exit(1);
    }
}

void pulse_data_print_vcd_header(FILE *file, uint32_t sample_rate)
{
    if (!file) {
        FATAL("Invalid stream in pulse_data_print_vcd_header()");
    }

    char time_str[LOCAL_TIME_BUFLEN];
    char *timescale;
    if (sample_rate <= 500000)
        timescale = "1 us";
    else
        timescale = "100 ns";
    chk_ret(fprintf(file, "$date %s $end\n", format_time_str(time_str, NULL, 0, 0)));
    chk_ret(fprintf(file, "$version rtl_433 0.1.0 $end\n"));
    chk_ret(fprintf(file, "$comment Acquisition at %s Hz $end\n", nice_freq(sample_rate)));
    chk_ret(fprintf(file, "$timescale %s $end\n", timescale));
    chk_ret(fprintf(file, "$scope module rtl_433 $end\n"));
    chk_ret(fprintf(file, "$var wire 1 / FRAME $end\n"));
    chk_ret(fprintf(file, "$var wire 1 ' AM $end\n"));
    chk_ret(fprintf(file, "$var wire 1 \" FM $end\n"));
    chk_ret(fprintf(file, "$upscope $end\n"));
    chk_ret(fprintf(file, "$enddefinitions $end\n"));
    chk_ret(fprintf(file, "#0 0/ 0' 0\"\n"));
}

void pulse_data_print_vcd(FILE *file, pulse_data_t const *data, int ch_id)
{
    float scale;
    if (data->sample_rate <= 500000)
        scale = 1000000 / data->sample_rate; // unit: 1 us
    else
        scale = 10000000 / data->sample_rate; // unit: 100 ns
    uint64_t pos = data->offset;
    for (unsigned n = 0; n < data->num_pulses; ++n) {
        if (n == 0)
            chk_ret(fprintf(file, "#%.f 1/ 1%c\n", pos * scale, ch_id));
        else
            chk_ret(fprintf(file, "#%.f 1%c\n", pos * scale, ch_id));
        pos += data->pulse[n];
        chk_ret(fprintf(file, "#%.f 0%c\n", pos * scale, ch_id));
        pos += data->gap[n];
    }
    if (data->num_pulses > 0)
        chk_ret(fprintf(file, "#%.f 0/\n", pos * scale));
}

void pulse_data_load(FILE *file, pulse_data_t *data, uint32_t sample_rate)
{
    char s[1024];
    int i    = 0;
    int size = sizeof(data->pulse) / sizeof(*data->pulse);

    pulse_data_clear(data);
    data->sample_rate = sample_rate;
    double to_sample  = sample_rate / 1e6;
    // read line-by-line
    while (i < size && fgets(s, sizeof(s), file)) {
        // TODO: we should parse sample rate and timescale
        if (!strncmp(s, ";freq1", 6)) {
            data->freq1_hz = strtol(s + 6, NULL, 10);
        }
        if (!strncmp(s, ";freq2", 6)) {
            data->freq2_hz = strtol(s + 6, NULL, 10);
        }
        if (*s == ';') {
            if (i) {
                break; // end or next header found
            }
            else {
                continue; // still reading a header
            }
        }
        if (rfraw_check(s)) {
            rfraw_parse(data, s);
            i = data->num_pulses;
            continue;
        }
        // parse two ints.
        char *p = s;
        char *endptr;
        long mark  = strtol(p, &endptr, 10);
        p          = endptr + 1;
        long space = strtol(p, &endptr, 10);
        // fprintf(stderr, "read: mark %ld space %ld\n", mark, space);
        data->pulse[i] = (int)(to_sample * mark);
        data->gap[i++] = (int)(to_sample * space);
    }
    // fprintf(stderr, "read %d pulses\n", i);
    data->num_pulses = i;
}

void pulse_data_print_pulse_header(FILE *file)
{
    if (!file) {
        FATAL("Invalid stream in pulse_data_print_pulse_header()");
    }

    char time_str[LOCAL_TIME_BUFLEN];

    chk_ret(fprintf(file, ";pulse data\n"));
    chk_ret(fprintf(file, ";version 1\n"));
    chk_ret(fprintf(file, ";timescale 1us\n"));
    // chk_ret(fprintf(file, ";samplerate %u\n", data->sample_rate));
    chk_ret(fprintf(file, ";created %s\n", format_time_str(time_str, NULL, 1, 0)));
}

void pulse_data_dump(FILE *file, pulse_data_t const *data)
{
    if (!file) {
        FATAL("Invalid stream in pulse_data_dump()");
    }

    char time_str[LOCAL_TIME_BUFLEN];

    chk_ret(fprintf(file, ";received %s\n", format_time_str(time_str, NULL, 1, 0)));
    if (data->fsk_f2_est) {
        chk_ret(fprintf(file, ";fsk %u pulses\n", data->num_pulses));
        chk_ret(fprintf(file, ";freq1 %.0f\n", data->freq1_hz));
        chk_ret(fprintf(file, ";freq2 %.0f\n", data->freq2_hz));
    }
    else {
        chk_ret(fprintf(file, ";ook %u pulses\n", data->num_pulses));
        chk_ret(fprintf(file, ";freq1 %.0f\n", data->freq1_hz));
    }
    chk_ret(fprintf(file, ";centerfreq %.0f Hz\n", data->centerfreq_hz));
    chk_ret(fprintf(file, ";samplerate %u Hz\n", data->sample_rate));
    chk_ret(fprintf(file, ";sampledepth %u bits\n", data->depth_bits));
    chk_ret(fprintf(file, ";range %.1f dB\n", data->range_db));
    chk_ret(fprintf(file, ";rssi %.1f dB\n", data->rssi_db));
    chk_ret(fprintf(file, ";snr %.1f dB\n", data->snr_db));
    chk_ret(fprintf(file, ";noise %.1f dB\n", data->noise_db));

    double to_us = 1e6 / data->sample_rate;
    for (unsigned i = 0; i < data->num_pulses; ++i) {
        chk_ret(fprintf(file, "%.0f %.0f\n", data->pulse[i] * to_us, data->gap[i] * to_us));
    }
    chk_ret(fprintf(file, ";end\n"));
}

data_t *pulse_data_print_data(pulse_data_t const *data)
{
    int pulses[2 * PD_MAX_PULSES];
    double to_us = 1e6 / data->sample_rate;
    for (unsigned i = 0; i < data->num_pulses; ++i) {
        pulses[i * 2 + 0] = data->pulse[i] * to_us;
        pulses[i * 2 + 1] = data->gap[i] * to_us;
    }

    /* clang-format off */
    return data_make(
            "mod",              "", DATA_STRING, (data->fsk_f2_est) ? "FSK" : "OOK",
            "count",            "", DATA_INT,    data->num_pulses,
            "pulses",           "", DATA_ARRAY,  data_array(2 * data->num_pulses, DATA_INT, pulses),
            "freq1_Hz",         "", DATA_FORMAT, "%u Hz", DATA_INT, (unsigned)data->freq1_hz,
            "freq2_Hz",         "", DATA_COND,   data->fsk_f2_est, DATA_FORMAT, "%u Hz", DATA_INT, (unsigned)data->freq2_hz,
            "freq_Hz",          "", DATA_INT,    (unsigned)data->centerfreq_hz,
            "rate_Hz",          "", DATA_INT,    data->sample_rate,
            "depth_bits",       "", DATA_INT,    data->depth_bits,
            "range_dB",         "", DATA_FORMAT, "%.1f dB", DATA_DOUBLE, data->range_db,
            "rssi_dB",          "", DATA_FORMAT, "%.1f dB", DATA_DOUBLE, data->rssi_db,
            "snr_dB",           "", DATA_FORMAT, "%.1f dB", DATA_DOUBLE, data->snr_db,
            "noise_dB",         "", DATA_FORMAT, "%.1f dB", DATA_DOUBLE, data->noise_db,
            NULL);
    /* clang-format on */
}
