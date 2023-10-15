/** @file
    Cotech 36-7959 Weatherstation.

    Copyright (C) 2020 Andreas Holmberg <andreas@burken.se>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Cotech 36-7959 Weatherstation, 433Mhz.

Also: SwitchDoc Labs Weather FT020T.
Also: Sainlogic Weather Station WS019T
Also: Sainlogic Weather Station FT0300
Also: Ragova WiFi Weather Station FT-0310
Also: NicetyMeter Weather Station 0366 (without Lux or UV index)

OOK modulated with Manchester encoding, halfbit-width 500 us.
Message length is 112 bit, every second time it will transmit two identical messages, packet gap 5400 us.
Example raw message: {112}c6880000e80000846d20fffbfb39

Integrity check is done using CRC8 using poly=0x31  init=0xc0

Message layout

    AAAA BBBBBBBB C D E F GGGGGGGG HHHHHHHH IIIIIIII JJJJ KKKKKKKKKKKK LLLL MMMMMMMMMMMM NNNNNNNN OOOOOOOOOOOOOOOO PPPPPPPP XXXXXXXX

- A : 4 bit: ?? Type code? part of id?, never seems to change
- B : 8 bit: Id, changes when reset
- C : 1 bit: Battery indicator 0 = Ok, 1 = Battery low
- D : 1 bit: MSB of Wind direction
- E : 1 bit: MSB of Wind Gust value
- F : 1 bit: MSB of Wind Avg value
- G : 8 bit: Wind Avg, scaled by 10
- H : 8 bit: Wind Gust, scaled by 10
- I : 8 bit: Wind direction in degrees.
- J : 4 bit: ? Might belong to the rain value
- K : 12 bit: Total rain in mm, scaled by 10
- L : 4 bit: Flag bitmask, always the same sequence: 1000
- M : 12 bit: Temperature in Fahrenheit, offset 400, scaled by 10
- N : 8 bit: Humidity
- O : 16 bit: Sunlight intensity, 0 to 200,000 lumens
- P : 8 bit: UV index (1-15)
- X : 8 bit: CRC, poly 0x31, init 0xc0
*/

static int cotech_36_7959_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x01, 0x40}; // 12 bits

    int r = -1;
    uint8_t b[14]; // 112 bits are 14 bytes
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
    int flags     = (b[7] & 0xf0) >> 4;                           // [56:4]
    int temp_raw  = ((b[7] & 0x0f) << 8) | (b[8]);                // [60:12]
    int humidity  = (b[9]);                                       // [72:8]
    int light_lux = (b[10] << 8) + b[11] + ((flags & 0x08) << 9); // [80:16]
    int uv        = (b[12]);                                      // [96:8]
    //int crc       = (b[13]);                                      // [104:8]

    float temp_c = (temp_raw - 400) * 0.1f;

    // On models without a light sensor, the value read for UV index is out of bounds with its top bits set
    int light_is_valid = ((uv & 0xf0) == 0);

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "Cotech-367959",
            //"subtype",          "Type code",        DATA_INT, subtype,
            "id",               "ID",               DATA_INT,    id,
            "battery_ok",       "Battery",          DATA_INT,    !batt_low,
            "temperature_F",    "Temperature",      DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",         DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "rain_mm",          "Rain",             DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain * 0.1f,
            "wind_dir_deg",     "Wind direction",   DATA_INT,    wind_dir,
            "wind_avg_m_s",     "Wind",             DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind * 0.1f,
            "wind_max_m_s",     "Gust",             DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, gust * 0.1f,
            "light_lux",        "Light Intensity",  DATA_COND, light_is_valid, DATA_FORMAT, "%u lux", DATA_INT, light_lux,
            "uv",               "UV Index",         DATA_COND, light_is_valid, DATA_FORMAT, "%u", DATA_INT, uv,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const cotech_36_7959_output_fields[] = {
        "model",
        //"subtype",
        "id",
        "battery_ok",
        "temperature_F",
        "humidity",
        "rain_mm",
        "wind_dir_deg",
        "wind_avg_m_s",
        "wind_max_m_s",
        "light_lux",
        "uv",
        "mic",
        NULL,
};

r_device const cotech_36_7959 = {
        .name        = "Cotech 36-7959, SwitchDocLabs FT020T wireless weather station with USB",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 500,
        .long_width  = 0,    // not used
        .gap_limit   = 1200, // Not used
        .reset_limit = 1200, // Packet gap is 5400 us.
        .decode_fn   = &cotech_36_7959_decode,
        .fields      = cotech_36_7959_output_fields,
};
