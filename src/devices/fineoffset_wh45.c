/** @file
    Fine Offset Electronics WH45 air quality sensor.

    Copyright (C) 2022 \@anthyz

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Fine Offset Electronics WH45 air quality sensor,

- also Ecowitt WH45, Ecowitt WH0295
- also Froggit DP250
- also Ambient Weather AQIN

Preamble is aaaa aaaa, sync word is 2dd4.

Packet layout:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14
    YY II II II 0T TT HH Bp pp BP PP CC CC XX AA
    45 00 36 60 02 7e 36 40 23 00 29 02 29 07 4f

- Y: 8 bit fixed sensor type 0x45
- I: 24 bit device ID
- T: 11 bit temperature, offset 40, scale 10
- H: 8 bit humidity
- B: 1 bit MSB of battery bars out of 5 (a value of 6 indicates external power via USB)
- p: 14 bit PM2.5 reading in ug/m3 * 10
- B: 2 bits LSBs of battery bars out of 5
- P: 14 bit PM10 reading in ug/m3 * 10
- C: 16 bit CO2 reading in ppm
- X: 8 bit CRC
- A: 8 bit checksum

Older air quality sensors (WH0290/WH41/WH43) from Fine Offset use a
particulate sensor from Honeywell that crudely estimates PM10 values
from PM2.5 measurements. Though Ecowitt and other displays only show
PM2.5, the rtl_433 WH0290 decoder includes the estimated PM10 value.
See the WH0290 decoder for more details.

The WH45 uses a Sensirion SPS30 sensor for PM2.5/PM10 and a
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

static int fineoffset_wh45_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xaa, 0x2d, 0xd4}; // 24 bit, part of preamble and sync word
    uint8_t b[15];

    // bit counts have been observed between 187 and 222
    if (bitbuffer->bits_per_row[0] < 170 || bitbuffer->bits_per_row[0] > 240) {
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

    if (b[0] != 0x45) // Check for family code 0x45
        return DECODE_ABORT_EARLY;

    decoder_log_bitrow(decoder, 1, __func__, b, sizeof (b) * 8, "");

    // Verify checksum and CRC
    uint8_t crc = crc8(b, 13, 0x31, 0x00);
    uint8_t chk = add_bytes(b, 14);
    if (crc != b[13] || chk != b[14]) {
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

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Fineoffset-WH45",
            "id",               "ID",           DATA_FORMAT, "%06x", DATA_INT, id,
            "battery_ok",       "Battery Level",  DATA_FORMAT, "%.1f", DATA_DOUBLE, battery_ok,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "pm2_5_ug_m3",      "2.5um Fine Particulate Matter",  DATA_FORMAT, "%.1f ug/m3", DATA_DOUBLE, pm2_5,
            "pm10_ug_m3",       "10um Coarse Particulate Matter",  DATA_FORMAT, "%.1f ug/m3", DATA_DOUBLE, pm10,
            "co2_ppm",          "Carbon Dioxide", DATA_FORMAT, "%d ppm", DATA_INT, co2,
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
        "pm2_5_ug_m3",
        "pm10_0_ug_m3",
        "co2_ppm",
        "ext_power",
        "mic",
        NULL,
};

r_device const fineoffset_wh45 = {
        .name        = "Fine Offset Electronics WH45 air quality sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 2500,
        .decode_fn   = &fineoffset_wh45_decode,
        .fields      = output_fields,
};
