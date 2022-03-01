/** @file
    EMOS 6016 Sensors contains DCF77, Temp, Hum, Windspeed, Winddir.

    Copyright (C) 2022 Dirk Utke-Woehlke <kardinal26@mail.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

static uint8_t add_inverted(uint8_t *b, int len)
{
    int sum = 0;
    for (int i = 0; i < len; i++) {
        sum += b[i] ^ 0xff;
    }
    sum = (sum & 0xff) ^ 0xff;
    return sum;
}

/**
EMOS 6016 Sensors contains DCF77, Temp, Hum, Windspeed, Winddir.

DCF77 not supported at the currently.

- Manufacturer: EMOS
- Transmit Interval: every ~61 s
- Frequency: 433.92 MHz
- Modulation: OOK PWM

RAW DATA:

    [00] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 ff
    [01] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fe
    [02] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fd
    [03] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fc
    [04] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fb
    [05] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fa

BitBench String the raw data must be inverted

    MODEL?:8h8h8h ID?:8d BAT?4d SEC:30d CH:2d TEMP:12d HUM?8d WSPEED:8d WINDIR:4d ?4h CHK:8h REPEAT:8h

Decoded record

    MODEL?:aaa583 ID?:255 BAT?09 SEC:0359300195 CH:0 TEMP:0201 HUM?037 WSPEED:000 WINDIR:10 ?2 CHK:c7 REPEAT:00

*/

static int emos6016_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 120 - 8); // ignores the repeat byte

    if (r < 0) {
        decoder_log(decoder, 2, __func__, "Repeated row fail");
        return DECODE_ABORT_EARLY;
    }
    decoder_logf(decoder, 2, __func__, "Found row: %d", r);

    uint8_t *b = bitbuffer->bb[r];
    // we expect 120 bits
    if (bitbuffer->bits_per_row[r] != 120) {
        decoder_log(decoder, 2, __func__, "Length check fail");
        return DECODE_ABORT_LENGTH;
    }
    // model check 55 5a 7c
    if (b[0] != 0x55 || b[1] != 0x5a || b[2] != 0x7c) {
        decoder_log(decoder, 2, __func__, "Model check fail");
        return DECODE_ABORT_EARLY;
    }
    // check checksum
    if (add_inverted(b, 13) != b[13]) {
        decoder_log(decoder, 2, __func__, "Checksum fail");
        return DECODE_FAIL_MIC;
    }

    int id         = b[3];
    int battery    = ((b[4] & 0xf0) >> 4);
    int channel    = ((b[8] >> 4) & 0x3) + 1;
    int temp_raw   = ((b[8] & 0x0f) << 8) | b[9];
    float temp_c   = temp_raw >= 2048 ? (temp_raw - 4096) * 0.1 : temp_raw * 0.1;
    int humidity   = b[10];
    float speed_ms = b[11];
    int dir_raw    = (((b[12] & 0xf0) >> 4));
    float dir_deg  = dir_raw * 22.5f;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "EMOS-6016",
            "id",               "House Code",       DATA_INT,    id,
            "channel",          "Channel",          DATA_INT,    channel,
            "battery_ok",       "Battery_OK",       DATA_INT,    !!battery,
            "temperature_C",    "Temperature_C",    DATA_FORMAT, "%.1f", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",         DATA_FORMAT, "%u", DATA_INT, humidity,
            "wind_avg_m_s" ,    "WindSpeed m_s",    DATA_FORMAT, "%.1f",  DATA_DOUBLE, speed_ms,
            "wind_dir_deg"  ,   "Wind direction",   DATA_FORMAT, "%.1f",  DATA_DOUBLE, dir_deg,
            "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "wind_avg_m_s",
        "wind_dir_deg",
        "mic",
        NULL,
};
// n=EMOS6016,m=OOK_PWM,s=280,l=796,r=804,g=0,t=0,y=1836,rows>=3,bits=120
r_device emos6016 = {
        .name        = "EMOS 6016 DCF77, Temp, Hum, Windspeed, Winddir sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 280,
        .long_width  = 796,
        .gap_limit   = 3000,
        .reset_limit = 804,
        .sync_width  = 1836,
        .decode_fn   = &emos6016_decode,
        .fields      = output_fields,
};
