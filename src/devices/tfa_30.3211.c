/* Copyright (C) 2018 ionum - projekte
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 * TFA 30.3211.02 
 * 
 * 1970us pulse with variable gap (third pulse 3920 us)
 * Above 79% humidity, gap after third pulse is 5848 us
 * 
 * Bit 1 : 1970us pulse with 3888 us gap
 * Bit 0 : 1970us pulse with 1936 us gap
 * 
 * Demoding with -X "tfa_test:OOK_PPM_RAW:2900:5000:36500"
 * 
 * 74 bit (2 bit preamble and 72 bit data => 9 bytes => 18 nibbles)
 * 
 * Nibble       1   2    3   4    5   6    7   8    9   10   11  12   13  14   15  16   17  18
 *           PP ?HHHhhhh ??CCNIII IIIITTTT ttttuuuu ???????? ???????? ???????? ???????? ??????
 * 
 *     P = Preamble
 *     H = First digit humidity 7-bit 0=8,1=9 (Range from 20 - 99%)
 *     h = Second digit humidity
 *     C = Channel
 *     T = First digit temperatur
 *     t = Second digit temperatur
 *     u = Third digit temperatur
 *     N = Negative temperatur
 *     
 * 
 */

#include "rtl_433.h"
#include "util.h"
#include "data.h"

static int tfa_303211_callback (bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t b[9] = {0};

    /* length check */
    if (74 != bitbuffer->bits_per_row[0]) {
        if(debug_output) fprintf(stderr,"tfa_303211 wrong size (%i bits)\n",bitbuffer->bits_per_row[0]);
        return 0;
    }

	/* dropping 2 bit preamle */
    bitbuffer_extract_bytes(bitbuffer, 0, 2, b, 72);

    float const temp        = ((b[2] & 0x0F) * 10) + ((b[3] & 0xF0) >> 4) + ((b[3] & 0x0F) *0.1F);
    int const minus         = (b[1] & 0x08) >> 3;
    int const hum           = (b[0] & 0x70) >> 4;
    int const humidity      = (hum < 2 ? hum + 8 : hum) * 10 + (b[0] & 0x0F);
    int const sensor_id     = ((b[1] & 0x07) << 4) | ((b[2] & 0xF0) >> 4);
    int const battery_low   = 0;
    int const channel       = (b[1] & 0x30) >> 4;

    float tempC = (minus == 1 ? temp * -1 : temp);

    char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);

    data = data_make(
            "time",          "",            DATA_STRING, time_str,
            "model",         "",            DATA_STRING, "TFA 30.3211.02",
            "id",            "",            DATA_INT, sensor_id,
            "channel",       "",            DATA_INT, channel,
            "battery",       "Battery",     DATA_STRING, "??",
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, tempC,
            "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
            NULL);
    data_acquired_handler(data);

    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "channel",
    "battery",
    "temperature_C",
    "humidity",
    NULL
};

r_device tfa_30_3211 = {
    .name          = "TFA 30.3211.02",
    .modulation    = OOK_PULSE_PPM_RAW,
    .short_limit   = 2900,
    .long_limit    = 6000,
    .reset_limit   = 36500,
    .json_callback = &tfa_303211_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields         = output_fields
};
