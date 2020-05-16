/** @file
    Cotech 36-7959 Weatherstation.

    Copyright (C) 2020 Andreas Holmberg <andreas@burken.se>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Cotech 36-7959 Weatherstation, 433Mhz

OOK modulated with Manchester zerobit encoding
Message length is 112 bit, every second time it will transmit two identical messages and every second it will transmit one. 
Example raw message: {112}c6880000e80000846d20fffbfb39

Integrity check is done using CRC8 using poly=0x31  init=0xc0

Message layout

0000 11111111 2 3 4 5 66666666 77777777 88888888 9999 AAAAAAAAAAAA BBBB CCCCCCCCCCCC DDDDDDDD EEEEEEEEEEEEEEEEEEEEEEEE FFFFFFFF

0 = 4 bit: ?? Type code? part of id?, never seems to change
1 = 8 bit: Id, changes when reset
2 = 1 bit: Battery indicator 0 = Ok, 1 = Battery low
3 = 1 bit: 1 = Wind direction value is max byte value (255) + value given by the wind direction byte
4 = 1 bit: 1 = Gust value is max byte value (255) + value given by the gust byte
5 = 1 bit: 1 = Wind value is max byte value (255) + value given by the wind byte
6 = 8 bit: Average wind. Calculated with value from byte / 10.0f
7 = 8 bit: Gust. Calculated with value from byte / 10.0f
8 = 8 bit: Wind direction in degrees.
9 = 4 bit: ? Might belong to the rain value
A = 12 bit: Total rain in mm. Calculated with value from bits / 10.f
B = 4 bit: ?, always the same sequence: 1000
C = 12 bit: Temperature. Value - 400 / 10.0f = Temperature in fahrenheit
D = 8 bit: Humidity
E = 24 bit: ? Always the same, might be some padding for the CRC-calculation
F = 8 bit: CRC
*/

#include "decoder.h"

#define NUM_BITS 112

/// extract a number up to 32/64 bits from given offset with given bit length
static unsigned long extract_number(uint8_t *data, unsigned bit_offset, unsigned bit_count)
{
    unsigned pos = bit_offset / 8;            // the first byte we need
    unsigned shl = bit_offset - pos * 8;      // shift left we need to align
    unsigned len = (shl + bit_count + 7) / 8; // number of bytes we need
    unsigned shr = 8 * len - shl - bit_count; // actual shift right
//    fprintf(stderr, "pos: %d, shl: %d, len: %d, shr: %d\n", pos, shl, len, shr);
    unsigned long val = data[pos];
    val = (uint8_t)(val << shl) >> shl; // mask off top bits
    for (unsigned i = 1; i < len - 1; ++i) {
        val = val << 8 | data[pos + i];
    }
    // shift down and add the last bits, so we don't potentially loose the top bits
    if (len > 1)
        val = (val << (8 - shr)) | (data[pos + len - 1] >> shr);
    else
        val >>= shr;
    return val;
}

static int cotech_36_7959_decode(r_device *decoder, bitbuffer_t *bitbuffer){
    if (decoder->verbose > 1) {
        fprintf(stderr, "%s: Decode starting\n", __func__);

        fprintf(stderr, "Decoder settings \"%s\"\n", decoder->name);
        fprintf(stderr, "\tmodulation=%u, short_width=%.0f, long_width=%.0f, reset_limit=%.0f\n",
                decoder->modulation, decoder->short_width, decoder->long_width, decoder->reset_limit);

        fprintf(stderr, "%s: Nr. of rows: %d\n", __func__, bitbuffer->num_rows);
        fprintf(stderr, "%s: Bits per row: %d\n", __func__, bitbuffer->bits_per_row[0]);
    }

    if (bitbuffer->num_rows > 2 || bitbuffer->bits_per_row[0] < NUM_BITS) {
        if (decoder->verbose > 1) 
            fprintf(stderr, "%s: Aborting because of short bit length or too few rows\n", __func__);

        return DECODE_ABORT_EARLY;
    }

    int i;
    unsigned pos;
    unsigned len;
    int match_count = 0;
    int r = -1;
    bitrow_t tmp;
    data_t *data;
    uint8_t preamble_bits[] = { 0x01, 0x40 };
    unsigned preamble_len = 12;

    if (decoder->verbose > 1) {
        fprintf(stderr, "%s: Nr. of repeated rows: %d\n", __func__, r);
    }

    for (i = 0; i < bitbuffer->num_rows; i++) {
        unsigned pos = bitbuffer_search(bitbuffer, i, 0, preamble_bits, preamble_len);

        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: Bitbuffer length: %d\n", __func__, bitbuffer->bits_per_row[i]);
            fprintf(stderr, "%s: Pos: %d\n", __func__, pos);
        }

        if (pos < bitbuffer->bits_per_row[i]) {
            if (r < 0)
                r = i;
            match_count++;
            pos += preamble_len;
            unsigned len = bitbuffer->bits_per_row[i] - pos;
            bitbuffer_extract_bytes(bitbuffer, i, pos, tmp, len);
            memcpy(bitbuffer->bb[i], tmp, (len + 7) / 8);
            bitbuffer->bits_per_row[i] = len;
        }
    }

    if (!match_count){
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: Couldn't find any match: %d\n", __func__, match_count);
        }   
        return DECODE_FAIL_SANITY;
    }

    //We're looking for a 112 bit message
    if(bitbuffer->bits_per_row[0] != NUM_BITS){
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: Wrong bits per row: %d\n", __func__, bitbuffer->bits_per_row[0]);
        }
        return DECODE_ABORT_LENGTH;
    }

    //Check CRC8: poly=0x31  init=0xc0  refin=false  refout=false  xorout=0x00  check=0x0d  residue=0x00
    if(crc8(bitbuffer->bb[0], NUM_BITS/8, 0x31, 0xc0)){
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: CRC8 fail: %u\n", __func__, crc8(bitbuffer->bb[0], 8, 0x31, 0xc0));
        }

        return DECODE_FAIL_MIC;
    }

    //Extract data from buffer
    //int type_code = extract_number(bitbuffer->bb[0], 0, 4); //Not sure about this
    int id = extract_number(bitbuffer->bb[0], 4, 8); //Not sure about this, changes on battery change or when reset
    int batt_low = extract_number(bitbuffer->bb[0], 12, 1);
    int deg_loop = extract_number(bitbuffer->bb[0], 13, 1);
    int gust_loop = extract_number(bitbuffer->bb[0], 14, 1);
    int wind_loop = extract_number(bitbuffer->bb[0], 15, 1);
    int wind = extract_number(bitbuffer->bb[0], 16, 8);
    int gust = extract_number(bitbuffer->bb[0], 24, 8);
    int wind_dir = extract_number(bitbuffer->bb[0], 32, 8);
    //int ?? = extract_number(bitbuffer->bb[0], 40, 4);
    int rain = extract_number(bitbuffer->bb[0], 44, 12);
    //int ?? = extract_number(bitbuffer->bb[0], 56, 4);
    int temp_raw = extract_number(bitbuffer->bb[0], 60, 12);
    int humidity = extract_number(bitbuffer->bb[0], 72, 8);
    //int ?? = extract_number(bitbuffer->bb[0], 80, 24);

    data = data_make(
        "model",                                "",                 DATA_STRING, "Cotech 36-7959 wireless weather station with USB",
        //"type_code",                            "Type code",        DATA_INT, type_code,
        "id",                                   "ID",               DATA_INT, id,
        "battery_ok",                           "Battery",          DATA_INT, !batt_low,
        "temperature_F",                        "Temperature",      DATA_FORMAT, "%.1f", DATA_DOUBLE, ((temp_raw - 400) / 10.0f),
        "humidity",                             "Humidity",         DATA_INT, humidity,
        _X("rain_mm", "rain"),                  "Rain",             DATA_FORMAT, "%.1f", DATA_DOUBLE, rain / 10.0f,
        _X("wind_dir_deg", "wind_direction"),   "Wind direction",   DATA_INT, deg_loop?255+wind_dir:wind_dir,
        _X("wind_avg_m_s", "wind_speed_ms"),    "Wind",             DATA_FORMAT, "%.1f", DATA_DOUBLE, (wind_loop?255+wind:wind) / 10.0f,
        _X("wind_max_m_s", "gust_speed_ms"),    "Gust",             DATA_FORMAT, "%.1f", DATA_DOUBLE, (gust_loop?255+gust:gust) / 10.0f,
        "mic",                                  "Integrity",        DATA_STRING, "CRC",
        NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *cotech_36_7959_output_fields[] = {
    "model",
    //"type_code",
    "id",
    "battery_ok",
    "temperature_F",
    "humidity",
    "rain_mm",
    "wind_dir_deg",
    "wind_avg_m_s",
    "wind_max_m_s",
    "mic",
    NULL
};

r_device cotech_36_7959 = {
    .name           = "Cotech 36-7959 wireless weather station with USB",
    .modulation     = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_width    = 488,
    .long_width     = 0, //Not used
    .reset_limit    = 1200,
    .decode_fn      = &cotech_36_7959_decode,
    .disabled       = 0,
    .fields         = cotech_36_7959_output_fields,
};
