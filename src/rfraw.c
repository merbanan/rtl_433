/** @file
    RfRaw format functions.

    Copyright (C) 2020 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "rfraw.h"
#include "fatal.h"
#include <string.h>

static int hexstr_get_nibble(char const **p)
{
    if (!p || !*p || !**p) return -1;
    while (**p == ' ' || **p == '\t' || **p == '-' || **p == ':') ++*p;

    int c = **p;
    if (c >= '0' && c <= '9') {
        ++*p;
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        ++*p;
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        ++*p;
        return c - 'a' + 10;
    }

    return -1;
}

static int hexstr_get_byte(char const **p)
{
    int h = hexstr_get_nibble(p);
    int l = hexstr_get_nibble(p);
    if (h >= 0 && l >= 0)
        return (h << 4) | l;
    return -1;
}

static int hexstr_get_word(char const **p)
{
    int h = hexstr_get_byte(p);
    int l = hexstr_get_byte(p);
    if (h >= 0 && l >= 0)
        return (h << 8) | l;
    return -1;
}

static int hexstr_peek_byte(char const *p)
{
    int h = hexstr_get_nibble(&p);
    int l = hexstr_get_nibble(&p);
    if (h >= 0 && l >= 0)
        return (h << 4) | l;
    return -1;
}

bool rfraw_check(char const *p)
{
    // require 0xaa 0xb0 or 0xaa 0xb1
    return hexstr_get_nibble(&p) == 0xa
            && hexstr_get_nibble(&p) == 0xa
            && hexstr_get_nibble(&p) == 0xb
            && (hexstr_get_nibble(&p) | 1) == 0x1;
/*
    if (!p || !*p) return false;
    while (*p == ' ' || *p == '\t' || *p == '-' || *p == ':') ++p;
    if (*p != 'A' && *p != 'a') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '-' || *p == ':') ++p;
    if (*p != 'A' && *p != 'a') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '-' || *p == ':') ++p;
    if (*p != 'B' && *p != 'b') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '-' || *p == ':') ++p;
    if (*p != '0' && *p != '1') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '-' || *p == ':') ++p;
    if (*p != '0') return false;
    return true;
*/
}

static bool parse_rfraw(pulse_data_t *data, char const **p)
{
    if (!p || !*p || !**p) return false;

    int hdr = hexstr_get_byte(p);
    if (hdr !=0xaa) return false;

    int fmt = hexstr_get_byte(p);
    if (fmt != 0xb0 && fmt != 0xb1)
        return false;

    if (fmt == 0xb0) {
        hexstr_get_byte(p); // ignore len
    }

    int bins_len = hexstr_get_byte(p);
    if (bins_len > 8) return false;

    int repeats = 1;
    if (fmt == 0xb0) {
        repeats = hexstr_get_byte(p);
    }

    int bins[8] = {0};
    for (int i = 0; i < bins_len; ++i) {
        bins[i] = hexstr_get_word(p);
    }

    // check if this is the old or new format
    bool oldfmt = true;
    char const *t = *p;
    while (*t) {
        int b = hexstr_get_byte(&t);
        if (b < 0 || b == 0x55) {
            break;
        }
        if (b & 0x88) {
            oldfmt = false;
            break;
        }
    }

    unsigned prev_pulses = data->num_pulses;
    bool pulse_needed = true;
    bool aligned = true;
    while (*p) {
        if (aligned && hexstr_peek_byte(*p) == 0x55) {
            hexstr_get_byte(p); // consume 0x55
            break;
        }

        int w = hexstr_get_nibble(p);
        aligned = !aligned;
        if (w < 0) return false;
        if (w >= 8 || (oldfmt && !aligned)) { // pulse
            if (!pulse_needed) {
                data->gap[data->num_pulses] = 0;
                data->num_pulses++;
            }
            data->pulse[data->num_pulses] = bins[w & 7];
            pulse_needed = false;
        }
        else { // gap
            if (pulse_needed) {
                data->pulse[data->num_pulses] = 0;
            }
            data->gap[data->num_pulses] = bins[w];
            data->num_pulses++;
            pulse_needed = true;
        }
    }
    //data->gap[data->num_pulses - 1] = 3000; // TODO: extend last gap?

    unsigned pkt_pulses = data->num_pulses - prev_pulses;
    for (int i = 1; i < repeats && data->num_pulses + pkt_pulses <= PD_MAX_PULSES; ++i) {
        memcpy(&data->pulse[data->num_pulses], &data->pulse[prev_pulses], pkt_pulses * sizeof (*data->pulse));
        memcpy(&data->gap[data->num_pulses], &data->gap[prev_pulses], pkt_pulses * sizeof (*data->pulse));
        data->num_pulses += pkt_pulses;
    }
    //pulse_data_print(data);

    data->sample_rate = 1000000; // us
    return true;
}

bool rfraw_parse(pulse_data_t *data, char const *p)
{
    if (!p || !*p)
        return false;

    // don't reset pulse data
    // pulse_data_clear(data);

    while (*p) {
        // skip whitespace and separators
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '+' || *p == '-')
            ++p;

        if (!parse_rfraw(data, &p))
            break;
    }
    //pulse_data_print(data);
    return true;
}
