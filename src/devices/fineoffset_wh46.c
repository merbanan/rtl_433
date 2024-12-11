/** @file
    Fine Offset Electronics WH46 air quality sensor.

    Based on fineoffset_wh45 from \@anthyz
    Copyright (C) 2024 \@joanma747

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Fine Offset Electronics WH46 air quality sensor,

- also Ecowitt WH46

Preamble is aaaa aaaa, sync word is 2dd4.

Packet layout:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20
    YY II II II 0T TT HH Bp pp BP PP CC CC qq qq QQ QQ ?? ?? XX AA
    46 00 27 f1 02 b5 33 40 32 40 39 03 0b 00 2a 00 36 01 90 e4 16

- Y: 8 bit fixed sensor type 0x46
- I: 24 bit device ID
- T: 11 bit temperature, offset 40, scale 10
- H: 8 bit humidity
- B: 1 bit MSB of battery bars out of 5 (a value of 6 indicates external power via USB)
- p: 14 bit PM2.5 reading in ug/m3 * 10
- B: 2 bits LSBs of battery bars out of 5
- P: 14 bit PM10 reading in ug/m3 * 10
- C: 16 bit CO2 reading in ppm
- q: 14 bit PM1 reading in ug/m3 * 10
- Q: 14 bit PM4 reading in ug/m3 * 10
- ?: Constant value 0190. Might be version of a firmware or so.
- X: 8 bit CRC
- A: 8 bit checksum

The WH46 uses a Sensirion SPS30 sensor for PM1/PM2.5/PM4/PM10 and a
Sensirion SCD30 for CO2.

Technical documents for the SPS30 are here:

https://sensirion.com/products/catalog/SPS30

The sensor specification statement states that PM10 values are estimated
from distribution profiles of PM0.5, PM1.0, and PM2.5 measurements, but
the datasheet does a specify a degree of accuracy for the values unlike
the Honeywell sensor.

Technical documents for the SCD30 are here:

https://sensirion.com/products/catalog/SCD30/
*/

static int fineoffset_wh46_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xaa, 0x2d, 0xd4}; // 24 bit, part of preamble and sync word
    uint8_t b[21];

    // Find a data package and extract data buffer
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8;

    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) { // Did not find a big enough package
        decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "short package at %u", bit_offset);
        return DECODE_ABORT_LENGTH;
    }

    // Extract package data
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    if (b[0] != 0x46) // Check for family code 0x46
        return DECODE_ABORT_EARLY;

    decoder_log_bitrow(decoder, 1, __func__, b, sizeof (b) * 8, "");

    // Verify checksum and CRC
    uint8_t crc = crc8(b, 19, 0x31, 0x00);
    uint8_t chk = add_bytes(b, 20);
    if (crc != b[19] || chk != b[20]) {
        decoder_logf(decoder, 1, __func__, "Checksum error: %02x %02x", crc, chk);
        return DECODE_FAIL_MIC;
    }

    int id            = (b[1] << 16) | (b[2] << 8) | (b[3]);
    int temp_raw      = (b[4] & 0x7) << 8 | b[5];
    float temp_c      = (temp_raw - 400) * 0.1f;    // range -40.0-60.0 C
    int humidity      = b[6];
    int battery_bars  = (b[7] & 0x40) >> 4 | (b[9] & 0xC0) >> 6;
    // A battery bars value of 6 means the sensor is powered via USB (the Ecowitt WS View app shows 'DC')
    int ext_power     = battery_bars == 6 ? 1 : 0;
    //  Battery level is indicated with 5 bars. Convert to 0 (0 bars) to 1 (5 or 6 bars)
    float battery_ok  = MIN(battery_bars * 0.2f, 1.0f);
    int pm2_5_raw     = (b[7] & 0x3f) << 8 | b[8];
    float pm2_5       = pm2_5_raw * 0.1f;
    int pm10_raw      = (b[9] & 0x3f) << 8 | b[10];
    float pm10        = pm10_raw * 0.1f;
    int co2           = (b[11] << 8) | b[12];
    int pm1_raw       = (b[13] << 8) | b[14];
    float pm1         = pm1_raw * 0.1f;
    int pm4_raw       = (b[15] << 8) | b[16];
    float pm4         = pm4_raw * 0.1f;
    int unknown      = (b[17] << 8) | b[18];

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Fineoffset-WH46",
            "id",               "ID",           DATA_FORMAT, "%06x", DATA_INT, id,
            "battery_ok",       "Battery Level",  DATA_FORMAT, "%.1f", DATA_DOUBLE, battery_ok,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "pm1_ug_m3",        "1um Fine PM",  DATA_FORMAT, "%.1f ug/m3", DATA_DOUBLE, pm1,
            "pm2_5_ug_m3",      "2.5um Fine PM",  DATA_FORMAT, "%.1f ug/m3", DATA_DOUBLE, pm2_5,
            "pm4_ug_m3",        "4um Coarse PM",  DATA_FORMAT, "%.1f ug/m3", DATA_DOUBLE, pm4,
            "pm10_ug_m3",       "10um Coarse PM",  DATA_FORMAT, "%.1f ug/m3", DATA_DOUBLE, pm10,
            "co2_ppm",          "Carbon Dioxide", DATA_FORMAT, "%d ppm", DATA_INT, co2,
            "unknown",          "Do not know", DATA_FORMAT, "%d ?", DATA_INT, unknown,
            "ext_power",        "External Power", DATA_INT, ext_power,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "humidity",
        "pm1_ug_m3",
        "pm2_5_ug_m3",
        "pm4_ug_m3",
        "pm10_ug_m3",
        "co2_ppm",
        "unknown",
        "ext_power",
        "mic",
        NULL,
};

r_device const fineoffset_wh46 = {
        .name        = "Fine Offset Electronics WH46 air quality sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 2500,
        .decode_fn   = &fineoffset_wh46_decode,
        .fields      = output_fields,
};
