/** @file
    Template decoder for Raddy Weather Station, tested with WF-120C lite

    Copyright (C) 2025 Chad Matsalla

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/*
    - Run ./maintainer_update.py (needs a clean git stage or commit)
*/

#include "decoder.h"

/**
Raddy WF-100C Lite 

(describe the modulation, timing, and transmission, e.g.)
The device uses PPM encoding,
- 0 is encoded as 40 us pulse and 132 us gap,
- 1 is encoded as 40 us pulse and 224 us gap.
The device sends a transmission every 63 seconds.
A transmission starts with a preamble of 0xAA,
there a 5 repeated packets, each with a 1200 us gap.

(describe the data and payload, e.g.)
Data layout:
    (preferably use one character per bit)
    FFFFFFFF PPPPPPPP PPPPPPPP IIIIIIII IIIIIIII IIIIIIII TTTTTTTT TTTTTTTT CCCCCCCC
    (otherwise use one character per nibble if this fits well)
    FF PP PP II II II TT TT CC

- F: 8 bit flags, (0x40 is battery_low)
- P: 16-bit little-endian Pressure
- I: 24-bit little-endian id
- T: 16-bit little-endian Unknown, likely Temperature
- C: 8 bit Checksum, CRC-8 truncated poly 0x07 init 0x00
*/
static int raddy_wf100c_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x01, 0x40}; // 12 bits

    int r = -1;
    uint8_t b[16]; // 112 bits are 14 bytes
    data_t *data;

    if (bitbuffer->num_rows > 2) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[0] < 112 && bitbuffer->bits_per_row[1] < 112) {
        return DECODE_ABORT_EARLY;
    }

    for (int i = 0; i < bitbuffer->num_rows; ++i) {
        unsigned pos = bitbuffer_search(bitbuffer, i, 0, preamble, 12);
        pos += 12;

        if (pos + 112 > bitbuffer->bits_per_row[i])
            continue; // too short or not found

        r = i;
        bitbuffer_extract_bytes(bitbuffer, i, pos, b, 112);
        break;
    }

    if (r < 0) {
        decoder_log(decoder, 2, __func__, "Couldn't find preamble");
        return DECODE_FAIL_SANITY;
    }

    if (crc8(b, 14, 0x31, 0xc0)) {
        decoder_log(decoder, 2, __func__, "CRC8 fail");
        return DECODE_FAIL_MIC;
    }

    // Extract data from buffer
    //int subtype  = (b[0] >> 4);                                   // [0:4]
    int id        = ((b[0] & 0x0f) << 4) | (b[1] >> 4);           // [4:8]
    int batt_low  = (b[1] & 0x08) >> 3;                           // [12:1]
    int deg_msb   = (b[1] & 0x04) >> 2;                           // [13:1]
    int gust_msb  = (b[1] & 0x02) >> 1;                           // [14:1]
    int wind_msb  = (b[1] & 0x01);                                // [15:1]
    int wind      = (wind_msb << 8) | b[2];                       // [16:8]
    int gust      = (gust_msb << 8) | b[3];                       // [24:8]
    int wind_dir  = (deg_msb << 8) | b[4];                        // [32:8]
    //int rain_msb  = (b[5] >> 4);                                  // [40:4]
    int rain      = ((b[5] & 0x0f) << 8) | (b[6]);                // [44:12]
    //int flags     = (b[7] & 0xf0) >> 4;                           // [56:4]
    int temp_raw  = ((b[7] & 0x0f) << 8) | (b[8]);                // [60:12]
    int humidity  = (b[9]);
    uint32_t press_raw = (b[12] << 12) | (b[13] << 4) | (b[14] >> 4);
    double baro_inhg = press_raw / 50.0; 
    // int light_lux = (b[10] << 8) | b[11] | ((b[7] & 0x80) << 9);  // [56:1][80:16]
    // int uvi       = (b[12]);                                      // [96:8]
    //int crc       = (b[13]);                                      // [104:8]

    float temp_f = (temp_raw - 400) * 0.1f;

    // On models without a light sensor, the value read for UV index is out of bounds with its top bits set
    // int light_is_valid = (uvi <= 150); // error value seems to be 0xfb, lux would be 0xfffb

    char raw[32];
    for (int i = 0; i < 14; i++)
        sprintf(raw + i * 2, "%02x", b[i]);

    for (int i = 0; i < 14; i++) {
        printf("b[%d] = 0x%02x\n", i, b[i]);
    }



    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "Raddy-100C",
            //"subtype",          "Type code",        DATA_INT, subtype,
            "id",               "ID",               DATA_INT,    id,
            "battery_ok",       "Battery",          DATA_INT,    !batt_low,
            "temperature_F",    "Temperature (f)",      DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp_f,
            "temperature_C",    "Temperature (c)",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, (temp_f - 32) / 1.8,
            "rain_mm",          "Rain",             DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain * 0.1f,
            "wind_dir_deg",     "Wind direction",   DATA_INT,    wind_dir,
            "wind_avg_m_s",     "Wind",             DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind * 0.1f,
            "wind_max_m_s",     "Gust",             DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, gust * 0.1f,
            "humidity",         "Humidity",         DATA_INT, humidity,
            // "light_lux",        "Light Intensity",  DATA_COND, light_is_valid, DATA_FORMAT, "%u lux", DATA_INT, light_lux,
            // "uvi",              "UV Index",         DATA_COND, light_is_valid, DATA_FORMAT, "%.1f", DATA_DOUBLE, uvi * 0.1f,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            "raw_data",        "Raw Data",        DATA_STRING, raw,
            "press_raw",        "Pressure Raw",        DATA_INT, press_raw,
            "baro",        "Barometric Pressure",        DATA_FORMAT, "%.2f inHg", DATA_DOUBLE, baro_inhg,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
    }

    static char const *const raddy_wf100c_output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_F",
        "temperature_C",
        "humidity",
        "rain_mm",
        "wind_dir_deg",
        "wind_avg_m_s",
        "wind_max_m_s",
        "light_lux",
        "uvi",
        "mic",
        "raw_data",
        "baro",
        "press_raw",
        NULL,
};
r_device const raddy_wf100c = {
        .name        = "Raddy WF-100C Lite",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 500,
        .long_width  = 0,    // not used
        .gap_limit   = 1200, // Not used
        .reset_limit = 1200, // Packet gap is 5400 us.
        .decode_fn   = &raddy_wf100c_decode,
        .fields      = raddy_wf100c_output_fields,
};
