/** @file
    Fine Offset Electronics WS85 weather station.

    Copyright (C) 2022 Christian W. Zuckschwerdt <zany@triq.net>
    Protocol description by \@davidefa

    Copy of fineoffset_ws90.c with changes made to support Fine Offset WS85
    sensor array.  Changes made by John Pochmara <john@zoiedog.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Fine Offset Electronics WS85 weather station.

The WS85 is a WS90 with the removal of temperature, humidity, lux and uv.
Data bytes 1-13 are the same between the two models.  The new rain data
is in bytes 16-20, with bytes 19 and 20 reporting total rain.  Bytes
17 and 18 are affected by rain, but it is unknown what they report.  Byte
17 reports the voltage of the super cap. And the checksum and CRC
have been moved to bytes 27 and 26.  What is reported in the other
bytes is unknown at this time.

Also sold by EcoWitt.

Preamble is aaaa aaaa aaaa, sync word is 2dd4.

Packet layout:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    YY II II II BB FF UU WW DD GG UU UU RS UU UU R1 R2 SS UU UU UU UU UU UU UU UU XX AA
    85 00 28 EB 87 82 6F 00 83 00 3F FF 00 00 00 00 00 0B 00 00 FF EF FD 00 00 6B DD 0F 00 00 00

- Y = fixed sensor type 0x85
- I = device ID, might be less than 24 bit?
- B = battery voltage, unit of 20 mV, we assume a range of 3.0V to 1.4V
- F = flags and MSBs, 0x03: temp MSB, 0x10: wind MSB, 0x20: bearing MSB, 0x40: gust MSB
      0x80 or 0x08: maybe battery good? seems to be always 0x88
- W = wind speed, lowest 8 bits of wind speed, m/s, scale 10
- D = wind bearing, lowest 8 bits of wind bearing, range 0-359 deg, 0x1ff if invalid
- G = wind gust, lowest 8 bits of wind gust, m/s, scale 10
- U = unknown
- R = rain total (R3 << 8 | R4) * 0.1 mm
- RS = rain start dection ((R1 & 0x10) >>4), 1 = raining, 0 = not raining
- S = super cap voltage, unit of 0.1V, lower 6 bits, mask 0x3f
- Z = Firmware version. 0x82 = 130 = 1.3.0
- A = checksum
- X = CRC

Rain start info:
Status 1 will be reset to 0 when:
- Once the top is dry
- After the amount of water on the top has remained unchanged for two hours.

*/

static int fineoffset_ws85_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xaa, 0xaa, 0x2d, 0xd4}; // 32 bit, part of preamble and sync word
    uint8_t b[32];

    // Validate package, WS85 nominal size is 345 bit periods
    if (bitbuffer->bits_per_row[0] < 168 || bitbuffer->bits_per_row[0] > 500) {
        decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "abort length" );
        return DECODE_ABORT_LENGTH;
    }

    // Find a data package and extract data buffer
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, 32) + 32;
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) { // Did not find a big enough package
        decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "short package at %u (%u)", bit_offset, bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH;
    }

    // Extract package data
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    if (b[0] != 0x85) // Check for family code 0x85
        return DECODE_ABORT_EARLY;

    decoder_logf(decoder, 1, __func__, "WS85 detected, buffer is %u bits length", bitbuffer->bits_per_row[0]);

    // Verify checksum and CRC
    uint8_t crc = crc8(b, 26, 0x31, 0x00);
    uint8_t chk = add_bytes(b, 27);
    if (crc != b[26] || chk != b[27]) {
        decoder_logf(decoder, 1, __func__,
            "Checksum error: CRC=%02x (Expected=%02x) CHK=%02x (Expected=%02x)",
            crc, b[26], chk, b[27]);
        return DECODE_FAIL_MIC;
    }

    int id          = (b[1] << 16) | (b[2] << 8) | (b[3]);
    int battery_mv  = (b[4] * 20); // mV
    int flags       = b[5]; // to find the wind msb
    int wind_avg    = ((b[5] & 0x10) << 4) | (b[7]);
    int wind_dir    = ((b[5] & 0x20) << 3) | (b[8]);
    int wind_max    = ((b[5] & 0x40) << 2) | (b[9]);
    int rain_start  = (b[12] & 0x10) >> 4;
    int rain_raw    = (b[15] << 8) | b[16]; // Extract the 16-bit raw value
    int supercap_V  = (b[17] & 0x3f);
    int firmware    = b[25];

    int battery_ok = 0;
    if (battery_mv > 2400) {
        battery_ok = 1;
    }

    int battery_lvl = battery_mv < 1400 ? 0 : (battery_mv - 1400) / 16; // 1.4V-3.0V is 0-100
    if (battery_lvl > 100) {
        battery_lvl = 100;
    }

    char extra[31];
    snprintf(extra, sizeof(extra), "%02x%02x---%02x%02x%02x%02x%02x%02x%02x---%02x", b[13], b[14], /* b[15] b[16] is rain_raw, b[17] is supercap_V */ b[18], b[19], b[20], b[21], b[22], b[23], b[24], b[28]);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Fineoffset-WS85",
            "id",               "ID",               DATA_FORMAT, "%06x", DATA_INT,    id,
            "battery_ok",       "Battery",          DATA_INT, battery_ok, // 0–6 bars
            "battery_pct",      "Battery level",    DATA_INT, battery_lvl, // 0–100%
            "battery_mV",       "Battery Voltage",  DATA_FORMAT, "%d mV", DATA_INT,    battery_mv,
            "wind_dir_deg",     "Wind direction",   DATA_COND, wind_dir != 0x1ff,   DATA_INT, wind_dir,
            "wind_avg_m_s",     "Wind speed",       DATA_COND, wind_avg != 0x1ff,   DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind_avg * 0.1f,
            "wind_max_m_s",     "Gust speed",       DATA_COND, wind_max != 0x1ff,   DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind_max * 0.1f,
            "flags",            "Flags",            DATA_FORMAT, "%02x", DATA_INT, flags,
            "rain_mm",          "Total Rain",       DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_raw * 0.1f,
            "rain_start",       "Rain Start",       DATA_INT, rain_start,
            "supercap_V",       "Supercap Voltage", DATA_COND, supercap_V != 0xff, DATA_FORMAT, "%.1f V", DATA_DOUBLE, supercap_V * 0.1f,
            "firmware",         "Firmware Version", DATA_INT, firmware,
            "data",             "Extra Data",       DATA_STRING, extra,
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
        "battery_pct",
        "battery_mV",
        "wind_dir_deg",
        "wind_avg_m_s",
        "wind_max_m_s",
        "flags",
        "unknown",
        "rain_mm",
        "rain_start",
        "supercap_V",
        "firmware",
        "data",
        "mic",
        NULL,
};

r_device const fineoffset_ws85 = {
        .name        = "Fine Offset Electronics WS85 weather station",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 3000,
        .decode_fn   = &fineoffset_ws85_decode,
        .fields      = output_fields,
};
