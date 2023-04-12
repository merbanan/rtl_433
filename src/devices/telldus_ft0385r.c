/** @file
    Telldus weather station indoor unit FT0385R.

    Copyright (C) 2021 Jarkko Sonninen <kasper@iki.fi>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Telldus weather station indoor unit.

As the indoor unit receives a message from the outdoor unit,
it sends 3 radio messages
- Oregon-WGR800
- Oregon-THGR810 or Oregon-PCR800
- Telldus-FT0385R (this one)

The outdoor unit is the same as SwitchDoc Labs WeatherSense FT020T
and Cotech 36-7959 Weatherstation.

433Mhz, OOK modulated with Manchester encoding, halfbit-width 500 us.
Message length is 5 + 296 bit.
Each message starts with bits 10100 1110. First 9 bits is considered as a preamble.
The first 5 bits of the preamble is ignored and the rest of the message is used in CRC calculation.

Example raw message:

    {298} e1 23 00 0c 17 2b 0b 5a 09 34 00 00 00 00 00 03 00 1b 03 90 12 1b 12 1b 43 6e 4c 92 23 27 49 28 c8 ff fa fa 4b

Example raw message, if outdoor data is unavailable:

    {298} e0 73 7f fb fb fb fb fb fb fb ff fb ff fb 3f fb ff fb ff fb ff fb ff fb 47 fb 7b 6c 26 27 0a 27 93 ff fb fb 97

Integrity check is done using CRC8 using poly=0x31  init=0xc0

Message layout

    AAAABBBB BBBBCCCC ZJIHGFED DDDDDDDD EEEEEEEE FFFFFFFF GGGGGGGG HHHHHHHH IIIIIIII JJJJJJJJ
    KKKKKKKK KKKKKKKK LLLLLLLL LLLLLLLL MMMMMMMM MMMMMMMM NNNNNNNN NNNNNNNN OOOOOOOO OOOOOOOO PPPPPPPP PPPPPPPP
    SSSSQQQQ QQQQQQQQ RRRRRRRR SSSSSSSS TTTTTTTT UUUUUUUU UUUUUUUU VVVVVVVV VVVVVVVV
    WWWWWWWW WWWWWWWW XXXXXXXX YYYYYYYY

- A : 4 bit: ? Type code ?, fixed 0xe
- B : 8 bit: ? Indoor serial number or flags. Changes in reset.
- C : 4 bit: ? Flags, normally 0x3, Battery indicator 0 = Ok, 4 = Battery low ?
- Z : 1 bit: ? Unknown, possibly not used
- D : 9 bit: Wind Avg, scaled by 10. MSB in byte 2
- E : 9 bit: Wind Gust, scaled by 10. MSB in byte 2
- F : 9 bit: Wind direction in degrees. MSB in byte 2
- G : 9 bit: ? Wind 2, scaled by 10. MSB in byte 2
- H : 9 bit: ? Wind direction 2 in degrees. MSB in byte 2
- I : 9 bit: ? Wind 3, scaled by 10. MSB in byte 2
- J : 9 bit: ? Wind direction 3 in degrees. MSB in byte 2
- K : 16 bit: ? Rain rate in mm, scaled by 10
- L : 16 bit: Rain 1h mm, scaled by 10
- M : 16 bit: Rain 24h mm, scaled by 10. Unavailable value = 0x3ffb.
- N : 16 bit: Rain week mm, scaled by 10
- O : 16 bit: Rain month mm, scaled by 10
- P : 16 bit: Rain total in mm, scaled by 10
- Q : 12 bit: Temperature in Fahrenheit, offset 400, scaled by 10
- R : 8 bit: Humidity
- S : 12 bit: Temperature indoor in Fahrenheit, offset 400, scaled by 10. MSB in byte 24.
- T : 8 bit: Humidity indoor
- U : 16 bit: Pressure absolute in hPa
- V : 16 bit: Pressure relative in hPa
- W : 16 bit: ? Light intensity. No sensor: 0xfffa, outdoor data is unavailable: 0xfffb
- X : 8 bit: ? UV index. No sensor: 0xfa, outdoor data is unavailable: 0xfb
- Y : 8 bit: CRC, poly 0x31, init 0xc0

If outdoor data is unavailable, the value is 0xfb, 0x1fb, 0x7fb or 0xfffb
Telldus outdoor unit is missing Light and UV sensors, but they may be seen in the messages.

*/

#include "decoder.h"

static int telldus_ft0385r_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x14, 0xe0}; // 9 bits

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
        unsigned pos = bitbuffer_search(bitbuffer, i, 0, preamble, 9);
        pos += 8;

        if (pos + 296 > bitbuffer->bits_per_row[i])
            continue; // too short or not found

        r = i;
        bitbuffer_extract_bytes(bitbuffer, i, pos, b, 296);
        break;
    }

    if (r < 0) {
        decoder_log(decoder, 2, __func__, "Couldn't find preamble");
        return DECODE_FAIL_SANITY;
    }

    if (crc8(b, 37, 0x31, 0xc0)) {
        decoder_log(decoder, 2, __func__, "CRC8 fail");
        return DECODE_FAIL_MIC;
    }

    // Extract data from buffer
    //int header    = (b[0] & 0xf0) >> 4;                           // [0:4]
    //int serial    = ((b[0] & 0x0f) << 4) | ((b[1] & 0xf0) >> 4);  // [8:8]
    //int flags     = (b[1] & 0x0f);                                // [12:4]
    //int unk16     = (b[1] & 0x80) >> 7;                           // [16:1]
    //int deg3_msb  = (b[2] & 0x40) >> 6;                           // [17:1]
    //int wind3_msb = (b[2] & 0x20) >> 5;                           // [18:1]
    //int deg2_msb  = (b[2] & 0x10) >> 4;                           // [19:1]
    //int wind2_msb = (b[2] & 0x08) >> 3;                           // [20:1]
    int deg_msb   = (b[2] & 0x04) >> 2;                           // [21:1]
    int gust_msb  = (b[2] & 0x02) >> 1;                           // [22:1]
    int wind_msb  = (b[2] & 0x01);                                // [23:1]
    int wind      = (wind_msb << 8) | b[3];                       // [24:8]
    int gust      = (gust_msb << 8) | b[4];                       // [32:8]
    int wind_dir  = (deg_msb << 8) | b[5];                        // [40:8]
    //int wind2     = (wind2_msb << 8) | b[6];                      // [48:8]
    //int wind2_dir = (deg2_msb << 8) | b[7];                       // [56:8]
    //int wind3     = (wind3_msb << 8) | b[8];                      // [64:8]
    //int wind3_dir = (deg3_msb << 8) | b[9];                       // [72:8]
    //int rain_rate = ((b[10]) << 8) | (b[11]);                     // [80:12]
    //int rain_1h   = ((b[12]) << 8) | (b[13]);                     // [96:12]
    //int rain_24h  = ((b[14]) << 8) | (b[15]);                     // [112:12]
    //int rain_week = ((b[16]) << 8) | (b[17]);                     // [128:12]
    //int rain_mon  = ((b[18]) << 8) | (b[19]);                     // [144:12]
    int rain_tot  = ((b[20]) << 8) | (b[21]);                     // [160:16]
    //int rain_tot2 = ((b[22]) << 8) | (b[23]);                     // [176:16]
    int temp2_msb  = (b[24] & 0xf0) >> 4;                         // [192:4]
    int temp_raw  = ((b[24] & 0x0f) << 8) | (b[25]);              // [196:12]
    int humidity  = (b[26]);                                      // [208:8]
    int temp2_raw = (temp2_msb << 8) | (b[27]);                   // [216:8]
    int humidity2 = (b[28]);                                      // [224:8]
    int pressure  = ((b[29]) << 8) | (b[30]);                     // [232:16]
    //int pressure2 = ((b[31]) << 8) | (b[32]);                     // [248:16]
    //int light_lux = ((b[33]) << 8) | (b[34]);                     // [264:16]
    //int uv        = (b[35]);                                      // [280:8]
    //int crc       = (b[36]);                                      // [288:8]

    //int batt_low  = (flags & 0x04) >> 3;
    float temp_f = (temp_raw - 400) * 0.1f;
    float temp2_f = (temp2_raw - 400) * 0.1f;

    /* clang-format off */
    if (temp_raw != 0x7fb) {
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
    } else {
        // No outdoor data
        data = data_make(
            "model",            "",                 DATA_STRING, "Telldus-FT0385R",
            //"battery_ok",       "Battery",          DATA_INT,    !batt_low,
            "temperature_2_F",  "Temperature in",   DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp2_f,
            "humidity_2",       "Humidity in",      DATA_FORMAT, "%u %%", DATA_INT, humidity2,
            "pressure_hPa",     "Pressure",         DATA_FORMAT, "%.01f hPa", DATA_DOUBLE, pressure * 0.1f,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    }
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const telldus_ft0385r_output_fields[] = {
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

r_device const telldus_ft0385r = {
        .name        = "Telldus weather station FT0385R sensors",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 500,
        .long_width  = 0, // not used
        .gap_limit   = 1200, // Not used
        .reset_limit = 2400,
        .decode_fn   = &telldus_ft0385r_decode,
        .fields      = telldus_ft0385r_output_fields,
};
