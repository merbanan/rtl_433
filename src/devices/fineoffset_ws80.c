/** @file
    Fine Offset Electronics WS80 weather station.

    Copyright (C) 2022 Christian W. Zuckschwerdt <zany@triq.net>
    Protocol description by \@davidefa

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Fine Offset Electronics WS80 weather station.

Also sold by EcoWitt, used with the weather station GW1000.

Preamble is aaaa aaaa aaaa, sync word is 2dd4.

Packet layout:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
    YY II II II LL LL BB FF TT HH WW DD GG VV UU UU AA XX
    80 0a 00 3b 00 00 88 8a 59 38 18 6d 1c 00 ff ff d8 df

- Y = fixed sensor type 0x80
- I = device ID, might be less than 24 bit?
- L = light value, unit of 10 Lux (or 0.078925 W/m2)
- B = battery voltage, unit of 20 mV, we assume a range of 3.0V to 1.4V
- F = flags and MSBs, 0x03: temp MSB, 0x10: wind MSB, 0x20: bearing MSB, 0x40: gust MSB
      0x80 or 0x08: maybe battery good? seems to be always 0x88
- T = temperature, lowest 8 bits of temperature, offset 40, scale 10
- H = humidity
- W = wind speed, lowest 8 bits of wind speed, m/s, scale 10
- D = wind bearing, lowest 8 bits of wind bearing, range 0-359 deg, 0x1ff if invalid
- G = wind gust, lowest 8 bits of wind gust, m/s, scale 10
- V = uv index, scale 10
- U = unknown, might be rain option
- A = checksum
- X = CRC

*/

static int fineoffset_ws80_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xaa, 0x2d, 0xd4}; // 24 bit, part of preamble and sync word
    uint8_t b[18];

    // Validate package, WS80 nominal size is 219 bit periods
    if (bitbuffer->bits_per_row[0] < 168 || bitbuffer->bits_per_row[0] > 240) {
        return DECODE_ABORT_LENGTH;
    }

    // Find a data package and extract data buffer
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, 24) + 24;
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) { // Did not find a big enough package
        decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "short package at %u", bit_offset);
        return DECODE_ABORT_LENGTH;
    }

    // Extract package data
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    if (b[0] != 0x80) // Check for family code 0x80
        return DECODE_ABORT_EARLY;

    // Verify checksum and CRC
    uint8_t crc = crc8(b, 17, 0x31, 0x00);
    uint8_t chk = add_bytes(b, 17);
    if (crc != 0 || chk != b[17]) {
        decoder_logf(decoder, 1, __func__, "Checksum error: %02x %02x", crc, chk);
        return DECODE_FAIL_MIC;
    }

    int id          = (b[1] << 16) | (b[2] << 8) | (b[3]);
    int light_raw   = (b[4] << 8) | (b[5]);
    float light_lux = light_raw * 10;        // Lux
    //float light_wm2 = light_raw * 0.078925f; // W/m2
    int battery_mv  = (b[6] * 20);            // mV
    int battery_lvl = battery_mv < 1400 ? 0 : (battery_mv - 1400) / 16; // 1.4V-3.0V is 0-100
    int flags       = b[7]; // to find the wind msb
    int temp_raw    = ((b[7] & 0x03) << 8) | (b[8]);
    float temp_c    = (temp_raw - 400) * 0.1f;
    int humidity    = (b[9]);
    int wind_avg    = ((b[7] & 0x10) << 4) | (b[10]);
    int wind_dir    = ((b[7] & 0x20) << 3) | (b[11]);
    int wind_max    = ((b[7] & 0x40) << 2) | (b[12]);
    int uv_index    = (b[13]);
    int unknown     = (b[14] << 8) | (b[15]);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Fineoffset-WS80",
            "id",               "ID",               DATA_FORMAT, "%06x", DATA_INT,    id,
            "battery_ok",       "Battery",          DATA_DOUBLE, battery_lvl * 0.01f,
            "battery_mV",       "Battery Voltage",  DATA_FORMAT, "%d mV", DATA_INT,    battery_mv,
            "temperature_C",    "Temperature",      DATA_COND, temp_raw != 0x3ff,   DATA_FORMAT, "%.1f C",   DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",         DATA_COND, humidity != 0xff,    DATA_FORMAT, "%u %%",    DATA_INT, humidity,
            "wind_dir_deg",     "Wind direction",   DATA_COND, wind_dir != 0x1ff,   DATA_INT, wind_dir,
            "wind_avg_m_s",     "Wind speed",       DATA_COND, wind_avg != 0x1ff,   DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind_avg * 0.1f,
            "wind_max_m_s",     "Gust speed",       DATA_COND, wind_max != 0x1ff,   DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind_max * 0.1f,
            "uvi",              "UVI",              DATA_COND, uv_index != 0xff,    DATA_FORMAT, "%.1f",     DATA_DOUBLE, uv_index * 0.1f,
            "light_lux",        "Light",            DATA_COND, light_raw != 0xffff, DATA_FORMAT, "%.1f lux", DATA_DOUBLE, (double)light_lux,
            "flags",            "Flags",            DATA_FORMAT, "%02x", DATA_INT, flags,
            "unknown",          "Unknown",          DATA_COND, unknown != 0x3fff, DATA_INT, unknown,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "battery_mV",
        "temperature_C",
        "humidity",
        "wind_dir_deg",
        "wind_avg_m_s",
        "wind_max_m_s",
        "uvi",
        "light_lux",
        "flags",
        "unknown",
        "mic",
        NULL,
};

r_device const fineoffset_ws80 = {
        .name        = "Fine Offset Electronics WS80 weather station",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 1500,
        .decode_fn   = &fineoffset_ws80_decode,
        .fields      = output_fields,
};
