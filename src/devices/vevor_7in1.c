/** @file
    Vevor Wireless Weather Station 7-in-1.

    Copyright (C) 2024 Bruno OCTAU (ProfBoc75)

    Based on Emax protocol

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Vevor Wireless Weather Station 7-in-1.

Manufacturer : Fujian Youtong Industries Co., Ltd. rebrand under Vevor name.
Reference:

- YT60231, Vevor Weather Station 7-in-1
- R53 / R56 Fujian Youtong Industries , FCC ID : https://fccid.io/2AQBD-R53, https://fccid.io/2AQBD-R56

S.a. issue #3020

Data Layout:

- Preamble/Syncword  .... AA AA AA CA CA 54

    Byte Position   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34
    Sample         AA 00 f8 f7 9d 02 e3 32 01 0e 03 02 0b 01 38 02 39 7a 86 e0 87 21 85 6a d0 08 da fa ab 2f 64 4a e3 00 00
                   AA KC II II BF TT TT HH 0W WW GG 0D DD RR RR UU LL LL xx SS yy ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ??

- K:  {4} Type of sensor, = 0x0
- C:  {4} Channel, = 0x0
- I: {16} Sensor ID
- BF: {8} Battery Flag 0x9d = battery low, 0x1d = normal battery, may be pairing button to be confirmed ?
- T: {12} temperature in C, offset 500, scale 10
- H:  {8} humidity %
- W: {16} Wind speed, scale 10, offset 257 (0x0101)
- G:  {8} Wind Gust m/s scale 1.5
- D: {12} Wind Direction, offset 257
- R: {16} Total Rain mm/m2, 0.4 mm/m²/tips , offset 257
- U:  {5} UV index from 0 to 16, offset 1
- L: {1 + 15 bit} Lux value, if first bit = 1 , then x 10 the 15 bit (offset 257).
- ?: unknown, fixed values
- A:  {4} fixed values of 0xA
- 0:  {4} fixed values of 0x0
- xx: {8} incremental value each tx
- S:  {8} checksum
- yy: {8} incremental value each tx yy = xx + 1

*/

#define VEVOR_MESSAGE_BITLEN     264

static int vevor_7in1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // preamble is ....aaaaaaaaaacaca54
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0xca, 0xca, 0x54};

    // Because of a gap false positive if LUX at max for weather station, only single row to be analyzed with expected 2 repeats inside the data.
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int ret = 0;
    int pos = 0;
    while ((pos = bitbuffer_search(bitbuffer, 0, pos, preamble_pattern, sizeof(preamble_pattern) * 8)) + VEVOR_MESSAGE_BITLEN <= bitbuffer->bits_per_row[0]) {

        if (pos >= bitbuffer->bits_per_row[0]) {
            decoder_log(decoder, 2, __func__, "Preamble not found");
            ret = DECODE_ABORT_EARLY;
            continue;
        }
        decoder_logf(decoder, 2, __func__, "Found Vevor preamble pos: %d", pos);

        pos += sizeof(preamble_pattern) * 8;
        // we expect at least 21 bytes
        if (pos + 21 * 8 > bitbuffer->bits_per_row[0]) {
            decoder_log(decoder, 2, __func__, "Length check fail");
            ret = DECODE_ABORT_LENGTH;
            continue;
        }
        uint8_t b[21] = {0};
        bitbuffer_extract_bytes(bitbuffer, 0, pos, b, sizeof(b) * 8);

        // verify checksum
        if ((add_bytes(b, 19) & 0xff) != b[19]) {
            decoder_log(decoder, 2, __func__, "Checksum fail");
            ret = DECODE_FAIL_MIC;
            continue;
        }

        //int kind        = ((b[1] & 0xf0) >> 4);
        int channel     = (b[1] & 0x0f);
        int id          = (b[2] << 8) | b[3];
        int battery_low = (b[4] & 0x80) >> 7;

        if (b[0] == 0xAA && b[1] == 0) {

            int temp_raw      = (b[5] << 8) | b[6];
            float temp_c      = (temp_raw - 500) * 0.1f;
            int humidity      = b[7];
            int wind_raw      = ((b[8] << 8) | b[9]) - 257; // need to remove 0x0101.
            float speed_kmh   = wind_raw / 10.0f  ; // wind_raw / 36.0f for m/s
            int gust_raw      = b[10];
            float gust_kmh    = gust_raw / 1.5f ; // gust_raw / 1.5f / 3.6f m/s, + 0.1f offset from the weather display
            int direction_deg = (((b[11] & 0x0f) << 8) | b[12]) - 257; // need to remove 0x101.
            int rain_raw      = ((b[13] << 8) | b[14]) - 257; // need to remove 0x101.
            float rain_mm     = rain_raw * 0.233f; // calculation is 0.43f but display is 0.5f
            int uv_index      = (b[15] & 0x1f) - 1;
            int light_lux     = ((b[16] << 8) | b[17]) - 257; // need to remove 0x0101.
            int lux_multi     = (light_lux & 0x8000) >> 15;

            if (lux_multi == 1) {
                light_lux = (light_lux & 0x7fff) * 10;
            }

            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",                 DATA_STRING, "Vevor-7in1",
                    "id",               "",                 DATA_FORMAT, "%04x", DATA_INT,    id,
                    "channel",          "Channel",          DATA_INT,    channel,
                    "battery_ok",       "Battery_OK",       DATA_INT,    !battery_low,
                    "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                    "humidity",         "Humidity",         DATA_FORMAT, "%u %%", DATA_INT, humidity,
                    "wind_avg_km_h",    "Wind avg speed",   DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, speed_kmh,
                    "wind_max_km_h",    "Wind max speed",   DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, gust_kmh,
                    "wind_dir_deg",     "Wind Direction",   DATA_INT,    direction_deg,
                    "rain_mm",          "Total rainfall",   DATA_FORMAT, "%.1f mm",  DATA_DOUBLE, rain_mm,
                    "uv",               "UV Index",         DATA_FORMAT, "%u", DATA_INT, uv_index,
                    "light_lux",        "Lux",              DATA_FORMAT, "%u", DATA_INT, light_lux,
                    "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            return 1;
        }
        pos += VEVOR_MESSAGE_BITLEN;
    }
    return ret;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "wind_avg_km_h",
        "wind_max_km_h",
        "rain_mm",
        "wind_dir_deg",
        "uv",
        "light_lux",
        "mic",
        NULL,
};

r_device const vevor_7in1 = {
        .name        = "Vevor Wireless Weather Station 7-in-1",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 87,
        .long_width  = 87,
        .reset_limit = 9000, // keep message is one row because of a possible gap in the message if LUX values are zeros
        .decode_fn   = &vevor_7in1_decode,
        .fields      = output_fields,
};
