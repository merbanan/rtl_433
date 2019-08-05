/** @file
    Decoder for Holman Industries WS5029 weather station

    Copyright (C) 2019 Ryan Mounce <ryan@mounce.com.au>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */
/**

a.k.a. Holman iWeather Station
https://www.holmanindustries.com.au/products/iweather-station/

Appears to be related to the Fine Offset WH1080 and Digitech XC0348.

Modulation: FSK PCM
Frequency: 917.0 MHz +- 40 kHz
10 kb/s bitrate, 100 us symbol/bit time

A transmission burst is sent every 57 seconds. Each burst consists of 3
repititions of the same 192 bit "package" separated by a 1 ms gap.

Package format:
Preamble            {48}0xAAAAAAAAAAAA
Header              {24}0x98F3A5
Payload             {96} see below
Checksum            {8}  unidentified
Trailer/Postamble   {16} ???

Payload format:
Byte (dec)  09 10 11 12 13 14 15 16 17 18 19 20
Nibble key  II II CC CH HR RR WW Dx xx xx xx xx

IIII        station ID (randomised on each battery insertion)
CCC         degrees C, signed, in multiples of 0.1 C
HH          humidity %
RRR         cumulative rain in multiples of 0.79 mm
WW          wind speed in km/h
D           wind direction (0 = N, 4 = E, 8 = S, 12 = W)
xxxxxxxxx   ???, usually zero


To get raw data
$ rtl_433 -f 917M -X 'name=WS5029,modulation=FSK_PCM,short=100,long=100,preamble={48}0xAAAAAAAAAAAA,reset=19200'

*/

#include "decoder.h"

static int wind_dir_degr[] = {0, 23, 45, 68, 90, 113, 135, 158, 180, 203, 225, 248, 270, 293, 315, 338};
static uint8_t preamble[9] = {0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x98,0xF3,0xA5};

static int holman_ws5029_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int found = 0;
    data_t *data;
    uint8_t br[24];

    if (bitbuffer->num_rows != 1) {
        return 0;
    }

    unsigned bits = bitbuffer->bits_per_row[0];

    // flex sometimes seems to imagine an extra bit at the start
    // and an extra 2-4 bits at the end
    // let's allow for the leading bit and the whole gap period
    if (bits < 192 || bits > 203) {
        return 0;
    }

    for (unsigned offset = 0; offset+192 <= bits; offset++) {
        bitbuffer_extract_bytes(bitbuffer, 0, offset, br, 192);
        if (memcmp(br, preamble, sizeof(preamble)) == 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        return 0;
    }

    // byte 21 looks like a checksum - no success with brute force
    /*
    for (uint8_t firstbyte = 0; firstbyte < 21; firstbyte++) {
        for (uint8_t poly=0; poly<255; poly++) {
            if (crc8(&br[firstbyte], 21-firstbyte, poly, 0x00) == br[21]) {
                printf("CORRECT CRC8 with offset %u poly 0x%x\n", firstbyte, poly);
            }
            if (crc8le(&br[firstbyte], 21-firstbyte, poly, 0x00) == br[21]) {
                printf("CORRECT CRC8LE with offset %u poly 0x%x\n", firstbyte, poly);
            }
        }
    }
    return 0;
    */

    int device_id     = (br[9] << 8) | br[10];
    short temp_raw    = ((br[11] & 0xff) << 4) | ((br[12] & 0xf0) >> 4);
    if (temp_raw & 0x0800) {
        temp_raw |= 0xF000;
    }
    float temperature = temp_raw * 0.1;
    int humidity      = ((br[12] & 0x0f) << 4) | ((br[13] & 0xf0) >> 4);
    int rain_raw      = ((br[13] & 0x0f) << 12) | br[14];
    float rain        = rain_raw * 0.79f;
    int speed         = br[15];
    int direction_deg = wind_dir_degr[(br[16] & 0xf0) >> 4];

    data = data_make(
        "model",         "",               DATA_STRING, _X("Holman-WS5029","Holman Industries WS5029 weather station"),
        "id",            "StationID",      DATA_FORMAT, "%04X",     DATA_INT,    device_id,
        "temperature_C", "Temperature",    DATA_FORMAT, "%.01f C",  DATA_DOUBLE, temperature,
        "humidity",      "Humidity",       DATA_FORMAT, "%u %%",    DATA_INT,    humidity,
        "rain_mm",       "Total rainfall", DATA_FORMAT, "%.01f mm", DATA_DOUBLE, rain,
        "wind_avg_km_h", "Wind avg speed", DATA_FORMAT, "%u km/h",  DATA_INT,    speed,
        "direction_deg", "Wind degrees",   DATA_INT,    direction_deg,
        NULL);
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
    NULL
};

r_device holman_ws5029 = {
    .name           = "Holman Industries WS5029 weather station",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 100,
    .long_width     = 100,
    .reset_limit    = 19200,
    .decode_fn      = &holman_ws5029_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
