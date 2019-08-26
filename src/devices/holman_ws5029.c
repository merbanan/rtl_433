/** @file
    Decoder for Holman Industries WS5029 weather station.

    Copyright (C) 2019 Ryan Mounce <ryan@mounce.com.au>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */
/**
Decoder for Holman Industries WS5029 weather station,
a.k.a. Holman iWeather Station.
https://www.holmanindustries.com.au/products/iweather-station/

Appears to be related to the Fine Offset WH1080 and Digitech XC0348.

- Modulation: FSK PCM
- Frequency: 917.0 MHz +- 40 kHz
- 10 kb/s bitrate, 100 us symbol/bit time

A transmission burst is sent every 57 seconds. Each burst consists of 3
repititions of the same 192 bit "package" separated by a 1 ms gap.

Package format:
- Preamble            {48}0xAAAAAAAAAAAA
- Header              {24}0x98F3A5
- Payload             {96} see below
- Checksum            {8}  unidentified
- Trailer/Postamble   {16} ???

Payload format:

    Byte (dec)  09 10 11 12 13 14 15 16 17 18 19 20
    Nibble key  II II CC CH HR RR WW Dx xx xx xx xx

- IIII        station ID (randomised on each battery insertion)
- CCC         degrees C, signed, in multiples of 0.1 C
- HH          humidity %
- RRR         cumulative rain in multiples of 0.79 mm
- WW          wind speed in km/h
- D           wind direction (0 = N, 4 = E, 8 = S, 12 = W)
- xxxxxxxxx   ???, usually zero

To get raw data
$ rtl_433 -f 917M -X 'name=WS5029,modulation=FSK_PCM,short=100,long=100,preamble={48}0xAAAAAAAAAAAA,reset=19200'

*/

#include "decoder.h"

static int holman_ws5029_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int const wind_dir_degr[] = {0, 23, 45, 68, 90, 113, 135, 158, 180, 203, 225, 248, 270, 293, 315, 338};
    uint8_t const preamble[] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0x98, 0xF3, 0xA5};

    data_t *data;
    uint8_t b[24];

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    unsigned bits = bitbuffer->bits_per_row[0];

    // FSK sometimes decodes an extra bit at the start
    // and likely extra 2-4 bits at the end
    // let's allow for the leading bit and the whole gap period
    if (bits < 192 || bits > 203) {
        return DECODE_ABORT_LENGTH;
    }

    unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof (preamble) * 8);
    if (offset + 192 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 192);

    // byte 21 looks like a checksum - no success with brute force
    /*
    for (uint8_t firstbyte = 0; firstbyte < 21; firstbyte++) {
        for (uint8_t poly=0; poly<255; poly++) {
            if (crc8(&b[firstbyte], 21-firstbyte, poly, 0x00) == b[21]) {
                fprintf(stderr, "CORRECT CRC8 with offset %u poly 0x%x\n", firstbyte, poly);
            }
            if (crc8le(&b[firstbyte], 21-firstbyte, poly, 0x00) == b[21]) {
                fprintf(stderr, "CORRECT CRC8LE with offset %u poly 0x%x\n", firstbyte, poly);
            }
        }
    }
    return 0;
    */

    int device_id     = (b[9] << 8) | b[10];
    int temp_raw      = (int16_t)((b[11] << 8) | (b[12] & 0xf0)) >> 4; // uses sign-extend
    float temp_c      = temp_raw * 0.1;
    int humidity      = ((b[12] & 0x0f) << 4) | ((b[13] & 0xf0) >> 4);
    int rain_raw      = ((b[13] & 0x0f) << 12) | b[14];
    float rain_mm     = rain_raw * 0.79f;
    int speed_kmh     = b[15];
    int direction_deg = wind_dir_degr[(b[16] & 0xf0) >> 4];

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "Holman-WS5029",
            "id",               "StationID",        DATA_FORMAT, "%04X",     DATA_INT,    device_id,
            "temperature_C",    "Temperature",      DATA_FORMAT, "%.01f C",  DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",         DATA_FORMAT, "%u %%",    DATA_INT,    humidity,
            "rain_mm",          "Total rainfall",   DATA_FORMAT, "%.01f mm", DATA_DOUBLE, rain_mm,
            "wind_avg_km_h",    "Wind avg speed",   DATA_FORMAT, "%u km/h",  DATA_INT,    speed_kmh,
            "direction_deg",    "Wind degrees",     DATA_INT,    direction_deg,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "humidity",
        "rain_mm",
        "wind_avg_km_h",
        "direction_deg",
        NULL,
};

r_device holman_ws5029 = {
        .name        = "Holman Industries WS5029 weather station",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 19200,
        .decode_fn   = &holman_ws5029_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
