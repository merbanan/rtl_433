/** @file
    Telldus weather station indoor unit FT0385R.

    Copyright (C) 2021 Jarkko Sonninen <kasper@iki.fi>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Telldus weather station indoor unit

As the indoor unit receives a message from the outdoor unit,
 it sends 3 radio messages
 * - Oregon-WGR800
 * - Oregon-THGR810 or Oregon-PCR800
 * - Telldus-FT0385R (this one)
The outdoor unit is the same as SwitchDoc Labs WeatherSense FT020T
 and Cotech 36-7959 Weatherstation.

433Mhz, OOK modulated with Manchester encoding, halfbit-width 500 us.
Message length is 5 + 296 bit. 
Each message starts with bits 10100 11100001 00100011. First 13 bits is considered as a preamble.
The first 5 bits of the preamble is ignored and the rest of the message is used in CRC calculation.
Example raw message: 
{298} e1 23 00 0c 17 2b 0b 5a 09 34 00 00 00 00 00 03 00 1b 03 90 12 1b 12 1b 43 6e 4c 92 23 27 49 28 c8 ff fa fa 4b 

Integrity check is done using CRC8 using poly=0x31  init=0xc0

Message layout
    AAAAAAAA BBBBBBBB CJIHGFED DDDDDDDD EEEEEEEE FFFFFFFF GGGGGGGG HHHHHHHH IIIIIIII JJJJJJJJ
    KKKKKKKKKKKKKKKK LLLLLLLLLLLLLLLL MMMMMMMMMMMMMMMM NNNNNNNNNNNNNNNN OOOOOOOOOOOOOOOO PPPPPPPPPPPPPPPP
    QQQQ RRRRRRRRRRRR SSSSSSSS TTTTTTTT UUUUUUUU VVVVVVVVVVVVVVVV WWWWWWWWWWWWWWWW 
    XXXXXXXXXXXXXXXXXXXXXXXX YYYYYYYY

- A : 8 bit: ? Type code, fixed 0xe1
- B : 8 bit: Length, fixed 0x23
- C : 1 bit: ? Battery indicator 0 = Ok, 1 = Battery low
- D : 9 bit: Wind Avg, scaled by 10. MSB in byte 2
- E : 9 bit: Wind Gust, scaled by 10. MSB in byte 2
- F : 9 bit: Wind direction in degrees. MSB in byte 2
- G : 9 bit: ? Wind 2, scaled by 10. MSB in byte 2
- H : 9 bit: ? Wind direction 2 in degrees. MSB in byte 2
- I : 9 bit: ? Wind 3, scaled by 10. MSB in byte 2
- J : 9 bit: ? Wind direction 3 in degrees. MSB in byte 2
- K : 16 bit: ? Rain rate in mm, scaled by 10
- L : 16 bit: Rain 1h mm, scaled by 10
- M : 16 bit: Rain 24h mm, scaled by 10
- N : 16 bit: Rain week mm, scaled by 10
- O : 16 bit: Rain month mm, scaled by 10
- P : 16 bit: Rain total in mm, scaled by 10
- Q : 4 bit: ? Flag bitmask, always the same sequence:  0100
- R : 12 bit: Temperature in Fahrenheit, offset 400, scaled by 10
- S : 8 bit: Humidity
- T : 8 bit: Temperature indoor in Fahrenheit, offset -624, scaled by 10
- U : 8 bit: Humidity indoor
- V : 16 bit: Pressure absolute in hPa
- W : 16 bit: Pressure relative in hPa
- X : 24 bit: ? Fixed 0xfffafa
- Y : 8 bit: CRC, poly 0x31, init 0xc0
*/

#include "decoder.h"

static int telldus_ft0385r_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x14, 0xe1}; // 13 bits

    int r = -1;
    uint8_t b[37]; // 296 bits, 37 bytes
    data_t *data;

    if (bitbuffer->num_rows > 2) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[0] < 296 && bitbuffer->bits_per_row[1] < 296) {
        return DECODE_ABORT_EARLY;
    }

    for (int i = 0; i < bitbuffer->num_rows; ++i) {
        unsigned pos = bitbuffer_search(bitbuffer, i, 0, preamble, 13);
        pos += 8;

        if (pos + 296 > bitbuffer->bits_per_row[i])
            continue; // too short or not found

        r = i;
        bitbuffer_extract_bytes(bitbuffer, i, pos, b, 296);
        break;
    }

    if (r < 0) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: Couldn't find preamble\n", __func__);
        }
        return DECODE_FAIL_SANITY;
    }

    if (crc8(b, 37, 0x31, 0xc0)) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: CRC8 fail\n", __func__);
        }
        return DECODE_FAIL_MIC;
    }

    // Extract data from buffer
    //int header  = b[0];                                         // [0:8]
    int length    = (b[1]);                                       // [8:8]
    int batt_low  = (b[2] & 0x80) >> 7;                           // [16:1]
    int deg3_msb  = (b[2] & 0x40) >> 6;                           // [17:1]
    int wind3_msb = (b[2] & 0x20) >> 5;                           // [18:1]
    int deg2_msb  = (b[2] & 0x10) >> 4;                           // [19:1]
    int wind2_msb = (b[2] & 0x08) >> 3;                           // [20:1]
    int deg_msb   = (b[2] & 0x04) >> 2;                           // [21:1]
    int gust_msb  = (b[2] & 0x02) >> 1;                           // [22:1]
    int wind_msb  = (b[2] & 0x01);                                // [23:1]
    int wind      = (wind_msb << 8) | b[3];                       // [24:8]
    int gust      = (gust_msb << 8) | b[4];                       // [32:8]
    int wind_dir  = (deg_msb << 8) | b[5];                        // [40:8]
    int wind2     = (wind2_msb << 8) | b[6];                      // [48:8]
    int wind2_dir = (deg2_msb << 8) | b[7];                       // [56:8]
    int wind3     = (wind3_msb << 8) | b[8];                      // [64:8]
    int wind3_dir = (deg3_msb << 8) | b[9];                       // [72:8]
    int rain_rate = ((b[10]) << 8) | (b[11]);                     // [80:12]
    int rain_1h   = ((b[12]) << 8) | (b[13]);                     // [96:12]
    int rain_24h  = ((b[14]) << 8) | (b[15]);                     // [112:12]
    int rain_week = ((b[16]) << 8) | (b[17]);                     // [128:12]
    int rain_mon  = ((b[18]) << 8) | (b[19]);                     // [144:12]
    int rain_tot  = ((b[20]) << 8) | (b[21]);                     // [160:16]
    int rain_tot2 = ((b[22]) << 8) | (b[23]);                     // [176:16]
    int unk192    = (b[24] >> 4);                                 // [192:4]
    int temp_raw  = ((b[24] & 0x0f) << 8) | (b[25]);              // [196:12]
    int humidity  = (b[26]);                                      // [208:8]
    int temp2_raw = (b[27]);                                      // [216:8]
    int humidity2 = (b[28]);                                      // [224:8]
    int pressure  = ((b[29]) << 8) | (b[30]);                     // [232:16]
    int pressure2 = ((b[31]) << 8) | (b[32]);                     // [248:16]
    int unk264    = (b[33]);                                      // [264:8]
    int unk272    = (b[34]);                                      // [272:8]
    int unk280    = (b[35]);                                      // [280:8]
    int crc       = (b[36]);                                      // [288:8]

    float temp_f = (temp_raw - 400) * 0.1f;
    float temp2_f = (temp2_raw + 624) * 0.1f;

    if (decoder->verbose > 0) {
        fprintf(stderr, "length = %02x %d\n", length, length);
        fprintf(stderr, "batt_low  = %01x %d\n", batt_low, batt_low);
        fprintf(stderr, "wind = %02x %d\n", wind, wind);
        fprintf(stderr, "gust = %02x %d\n", gust, gust);
        fprintf(stderr, "wind_dir = %02x %d\n", wind_dir, wind_dir);
        fprintf(stderr, "wind2 = %04x %d\n", wind2, wind2);
        fprintf(stderr, "wind2_dir = %04x %d\n", wind2_dir, wind2_dir);
        fprintf(stderr, "wind3 = %04x %d\n", wind3, wind3);
        fprintf(stderr, "wind3_dir = %04x %d\n", wind3_dir, wind3_dir);
        fprintf(stderr, "rain_rate = %04x %f mm\n", rain_rate, rain_rate * 0.1);
        fprintf(stderr, "rain_1h = %04x %f mm\n", rain_1h, rain_1h * 0.1);
        fprintf(stderr, "rain_24h = %04x %f mm\n", rain_24h, rain_24h * 0.1);
        fprintf(stderr, "rain_week = %04x %f mm\n", rain_week, rain_week * 0.1);
        fprintf(stderr, "rain_mon = %04x %f mm\n", rain_mon, rain_mon * 0.1);
        fprintf(stderr, "rain_tot = %04x %f mm\n", rain_tot, rain_tot * 0.1);
        fprintf(stderr, "rain_tot2 = %04x %d\n", rain_tot2, rain_tot2);
        fprintf(stderr, "unk192 = %02x %d\n", unk192, unk192);
        fprintf(stderr, "temp_raw = %04x %d\n", temp_raw, temp_raw);
        fprintf(stderr, "humidity = %04x %d %%\n", humidity, humidity);
        fprintf(stderr, "temp_indoor = %02x %d\n", temp2_raw, temp2_raw);
        fprintf(stderr, "humidity_indoor = %04x %d %%\n", humidity2, humidity2);
        fprintf(stderr, "pressure_abs = %04x %f\n", pressure, pressure * 0.1);
        fprintf(stderr, "pressure_rel = %04x %f\n", pressure2, pressure2 * 0.1);
        fprintf(stderr, "unk264 = %02x %d\n", unk264, unk264);
        fprintf(stderr, "unk272 = %02x %d\n", unk272, unk272);
        fprintf(stderr, "unk280 = %02x %d\n", unk280, unk280);
        fprintf(stderr, "crc = %02x %d\n", crc, crc);
        fprintf(stderr, "temp_f = %f F (%f C)\n", temp_f, (temp_f - 32) / 1.8);
        fprintf(stderr, "temp2_f = %f F (%f C)\n", temp2_f, (temp2_f - 32) / 1.8);
    }
 
    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "Telldus-FT0385R",
            //"battery_ok",       "Battery",          DATA_INT,    !batt_low,
            "temperature_F",    "Temperature",      DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp_f,
            "humidity",         "Humidity",         DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "temperature_2_F",  "Temperature in",   DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp2_f,
            "humidity_2",       "Humidity in",      DATA_FORMAT, "%u %%", DATA_INT, humidity2,
            "pressure_hPa",     "Pressure",         DATA_FORMAT, "%.01f hPa", DATA_DOUBLE, pressure * 0.1f,
            //"rain_rate_mm_h",   "Rain Rate",        DATA_FORMAT, "%.02f mm/h", DATA_DOUBLE, rain_rate * 0.1f,
            "rain_mm",          "Rain",             DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_tot * 0.1f,
            "wind_dir_deg",     "Wind direction",   DATA_INT,    wind_dir,
            "wind_avg_m_s",     "Wind",             DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind * 0.1f,
            "wind_max_m_s",     "Gust",             DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, gust * 0.1f,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *telldus_ft0385r_output_fields[] = {
        "model",
        "battery_ok",
        "temperature_F",
        "humidity",
        "temperature_2_F",
        "humidity_2",
        "pressure_hPa",
        "rain_rate_mm_h",
        "rain_mm",
        "wind_dir_deg",
        "wind_avg_m_s",
        "wind_max_m_s",
        "mic",
        NULL,
};

r_device telldus_ft0385r = {
        .name        = "Telldus weather station FT0385R sensors",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 500,
        .long_width  = 0, // not used
        .gap_limit   = 1200, // Not used
        .reset_limit = 2400,
        .decode_fn   = &telldus_ft0385r_decode,
        .fields      = telldus_ft0385r_output_fields,
};
