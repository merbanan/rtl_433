/** @file
    EMOS E6016 weatherstation with DCF77.

    Copyright (C) 2022 Dirk Utke-Woehlke <kardinal26@mail.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
EMOS E6016 weatherstation with DCF77.

- Manufacturer: EMOS
- Transmit Interval: every ~61 s
- Frequency: 433.92 MHz
- Modulation: OOK PWM, INVERTED

Data Layout:

    PP PP PP II ?K KK KK KK CT TT HH SS DF XX RR

- P: (24 bit) preamble
- I: (8 bit) ID
- ?: (2 bit) unknown
- K: (32 bit) datetime, fields are 6d-4d-5d 5d:6d:6d
- C: (2 bit) channel
- T: (12 bit) temperature, signed, scale 10
- H: (8 bit) humidity
- S: (8 bit) wind speed
- D: (4 bit) wind direction
- F: (4 bit) flags of (?B??), B is battery good indication
- X: (8 bit) checksum
- R: (8 bit) repeat counter

Raw data:

    [00] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 ff
    [01] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fe
    [02] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fd
    [03] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fc
    [04] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fb
    [05] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fa

Format string:

    MODEL?:8h8h8h ID?:8d ?2b DT:6d-4d-5dT5d:6d:6d CH:2d TEMP:12d HUM?8d WSPEED:8d WINDIR:4d BAT:4b CHK:8h REPEAT:8h

Decoded example:

    MODEL?:aaa583 ID?:255 ?10 DT:21-05-21T07:49:35 CH:0 TEMP:0201 HUM?037 WSPEED:000 WINDIR:10 BAT:1101 CHK:c7 REPEAT:00

*/

static int emos_e6016_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_prefix(bitbuffer, 3, 120 - 8); // ignores the repeat byte
    if (row < 0) {
        decoder_log(decoder, 2, __func__, "Repeated row fail");
        return DECODE_ABORT_EARLY;
    }
    decoder_logf(decoder, 2, __func__, "Found row: %d", row);

    uint8_t *b = bitbuffer->bb[row];
    // we expect 120 bits
    if (bitbuffer->bits_per_row[row] != 120) {
        decoder_log(decoder, 2, __func__, "Length check fail");
        return DECODE_ABORT_LENGTH;
    }

    // model check 55 5a 7c
    if (b[0] != 0x55 || b[1] != 0x5a || b[2] != 0x7c) {
        decoder_log(decoder, 2, __func__, "Model check fail");
        return DECODE_ABORT_EARLY;
    }

    bitbuffer_invert(bitbuffer);

    // check checksum
    if ((add_bytes(b, 13) & 0xff) != b[13]) {
        decoder_log(decoder, 2, __func__, "Checksum fail");
        return DECODE_FAIL_MIC;
    }

    int id         = b[3];
    int battery    = ((b[12] >> 2) & 0x1);
    unsigned dcf77 = ((b[4] & 0x3f) << 26) | (b[5] << 18) | (b[6] << 10) | (b[7] << 2) | (b[8] >> 6);
    int dcf77_sec  = ((dcf77 >> 0) & 0x3f);
    int dcf77_min  = ((dcf77 >> 6) & 0x3f);
    int dcf77_hour = ((dcf77 >> 12) & 0x1f);
    int dcf77_day  = ((dcf77 >> 17) & 0x1f);
    int dcf77_mth  = ((dcf77 >> 22) & 0x0f);
    int dcf77_year = ((dcf77 >> 26) & 0x3f);
    int channel    = ((b[8] >> 4) & 0x3) + 1;
    int temp_raw   = (int16_t)(((b[8] & 0x0f) << 12) | (b[9] << 4)); // use sign extend
    float temp_c   = (temp_raw >> 4) * 0.1f;
    int humidity   = b[10];
    float speed_ms = b[11] * 0.295;
    int dir_raw    = (((b[12] & 0xf0) >> 4));
    float dir_deg  = dir_raw * 22.5f;

    char dcf77_str[20]; // "2064-16-32T32:64:64"
    snprintf(dcf77_str, sizeof(dcf77_str), "%4d-%02d-%02dT%02d:%02d:%02d", dcf77_year + 2000, dcf77_mth, dcf77_day, dcf77_hour, dcf77_min, dcf77_sec);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "EMOS-E6016",
            "id",               "House Code",       DATA_INT,    id,
            "channel",          "Channel",          DATA_INT,    channel,
            "battery_ok",       "Battery_OK",       DATA_INT,    battery,
            "temperature_C",    "Temperature_C",    DATA_FORMAT, "%.1f", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",         DATA_FORMAT, "%u", DATA_INT, humidity,
            "wind_avg_m_s",     "WindSpeed m_s",    DATA_FORMAT, "%.1f",  DATA_DOUBLE, speed_ms,
            "wind_dir_deg",     "Wind direction",   DATA_FORMAT, "%.1f",  DATA_DOUBLE, dir_deg,
            "radio_clock",      "Radio Clock",      DATA_STRING, dcf77_str,
            "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "wind_avg_m_s",
        "wind_dir_deg",
        "radio_clock",
        "mic",
        NULL,
};
// n=EMOS-E6016,m=OOK_PWM,s=280,l=796,r=804,g=0,t=0,y=1836,rows>=3,bits=120
r_device const emos_e6016 = {
        .name        = "EMOS E6016 weatherstation with DCF77",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 280,
        .long_width  = 796,
        .gap_limit   = 3000,
        .reset_limit = 850,
        .sync_width  = 1836,
        .decode_fn   = &emos_e6016_decode,
        .fields      = output_fields,
};
