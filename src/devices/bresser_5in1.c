/* Decoder for Bresser Weather Center 5-in-1.
 *
 * Copyright (C) 2018 Daniel Krueger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The compact 5-in-1 multifunction outdoor sensor transmits the data
 * on 868.3 MHz.
 * The device uses FSK-PCM encoding,
 * The device sends a transmission every 12 seconds.
 * A transmission starts with a preamble of 0xAA.
 *
 * Decoding borrowed from https://github.com/andreafabrizi/BresserWeatherCenter
 *
 * Preamble:
 * aa aa aa aa aa 2d d4
 *
 * Packet payload without preamble (203 bits):
 *  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
 * -----------------------------------------------------------------------------
 * ee 93 7f f7 bf fb ef 9e fe ae bf ff ff 11 6c 80 08 40 04 10 61 01 51 40 00 00
 * ed 93 7f ff 0f ff ef b8 fe 7d bf ff ff 12 6c 80 00 f0 00 10 47 01 82 40 00 00
 * eb 93 7f eb 9f ee ef fc fc d6 bf ff ff 14 6c 80 14 60 11 10 03 03 29 40 00 00
 * ed 93 7f f7 cf f7 ef ed fc ce bf ff ff 12 6c 80 08 30 08 10 12 03 31 40 00 00
 * f1 fd 7f ff af ff ef bd fd b7 c9 ff ff 0e 02 80 00 50 00 10 42 02 48 36 00 00 00 00 (from https://github.com/merbanan/rtl_433/issues/719#issuecomment-388896758)
 * ee b7 7f ff 1f ff ef cb fe 7b d7 fc ff 11 48 80 00 e0 00 10 34 01 84 28 03 00 (from https://github.com/andreafabrizi/BresserWeatherCenter)
 * CC CC CC CC CC CC CC CC CC CC CC CC CC uu II  G GG DW WW    TT  T HH RR  R  t
 *
 * C = Check, inverted data of 13 byte further
 * u = unknown (data changes from packet to packet, but meaning is still unknown)
 * I = station ID (maybe)
 * G = wind gust in 1/10 m/s, BCD coded, GGG = 123 => 12.3 m/s
 * D = wind direction 0..F = N..NNE..E..S..W..NNW
 * W = wind speed in 1/10 m/s, BCD coded, WWW = 123 => 12.3 m/s
 * T = temperature in 1/10 °C, BCD coded, TTxT = 1203 => 31.2 °C
 * t = temperature sign, minus if unequal 0
 * H = humidity in percent, BCD coded, HH = 23 => 23 %
 * R = rain in mm, BCD coded, RRxR = 1203 => 31.2 mm
 *
 */

/* Use this as a starting point for a new decoder. */

#include "decoder.h"

static const uint8_t preamble_pattern[] = { 0xaa, 0xaa, 0xaa, 0x2d, 0xd4 };

static float get_temperature(const uint8_t* br) {
    int temp_raw = (br[20] & 0x0f)
                + ((br[20] & 0xf0) >> 4) * 10
                + (br[21] &0x0f) * 100;
    if (br[25] & 0x0f) {
        temp_raw *= -1;
    }
    return temp_raw / 10.0f;
}

static int get_humidity(const uint8_t* br) {
    return (br[22] & 0x0f) + ((br[22] & 0xf0) >> 4) * 10;
}

static const char* get_wind_direction_str(const uint8_t* br) {
    static const char* wind_dir_string[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",};
    return wind_dir_string[(br[17] & 0xf0) >> 4];
}

static float get_wind_direction_deg(const uint8_t* br) {
    return ((br[17] & 0xf0) >> 4) * 22.5f;
}

static float get_wind_gust(const uint8_t* br) {
    int wind_raw = (br[16] & 0x0f)
                + ((br[16] & 0xf0) >> 4) * 10
                + (br[15] & 0x0f) * 100;
    return wind_raw / 10.0f;
}

static float get_wind_avg(const uint8_t* br) {
    int wind_raw = (br[18] & 0x0f)
                + ((br[18] & 0xf0) >> 4) * 10
                + (br[17] & 0x0f) * 100;
    return wind_raw / 10.0f;
}

static float get_rain(const uint8_t* br) {
    int rain_raw = (br[23] & 0x0f)
                + ((br[23] & 0xf0) >> 4) * 10
                + (br[24] & 0x0f) * 100;
    return rain_raw / 10.0f;
}

static int bresser_5in1_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t msg[26];
    char msg_hex[sizeof(msg) * 2 + 1];
    uint16_t sensor_id;
    unsigned int len = 0;

    if (bitbuffer->num_rows != 1
        || bitbuffer->bits_per_row[0] < 248
        || bitbuffer->bits_per_row[0] > 440) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s bit_per_row %u out of range\n", __func__, bitbuffer->bits_per_row[0]);
        }
        return 0; // Unrecognized data
    }

    unsigned int start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof (preamble_pattern) * 8);

    if (start_pos == bitbuffer->bits_per_row[0]) {
        return 0;
    }
    start_pos += sizeof (preamble_pattern) * 8;
    len = bitbuffer->bits_per_row[0] - start_pos;
    if (((len + 7) / 8) < sizeof (msg)) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s %u too short\n", __func__, len);
        }
        return 0; // message too short
    }
    // truncate any excessive bits
    len = min(len, sizeof (msg) * 8);

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, len);

    // convert binary message to hex string
    for (unsigned int col = 0; col < (len + 7) / 8; ++col) {
        sprintf(&msg_hex[2 * col], "%02x", msg[col]);
    }

    /*
     * Check message integrity
     * First 13 bytes need to match inverse of last 13 bytes
     */
    for (unsigned int col = 0; col < (sizeof (msg) / 2); ++col) {
        uint8_t inv_msg = (msg[col] ^ 0xFF);
        if (inv_msg != msg[col + 13]) {
            if (decoder->verbose > 1) {
                fprintf(stderr, "%s MIC wrong at %u: %s\n", __func__, col, msg_hex);
            }
            return 0; // message isn't correct
        }
    }

    /*
     * Now that message "envelope" has been validated,
     * start parsing data.
     */

    sensor_id = msg[14];

    data = data_make(
            "model", "", DATA_STRING, "Bresser Weather Center 5-in-1",
            "id",    "", DATA_INT,    sensor_id,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, get_temperature(msg),
            "humidity","Humidity",  DATA_INT, get_humidity(msg),
            "gust",         "Gust",       DATA_FORMAT,  "%2.1f m/s",DATA_DOUBLE, get_wind_gust(msg),
            "speed",        "Average",    DATA_FORMAT,  "%2.1f m/s",DATA_DOUBLE, get_wind_avg(msg),
            "direction_deg","Direction",  DATA_FORMAT,  "%3.1f degrees",DATA_DOUBLE, get_wind_direction_deg(msg),
            "direction_str","Direction",  DATA_STRING,  get_wind_direction_str(msg),
            "rain",         "Rain",       DATA_FORMAT,  "%2.1f mm",DATA_DOUBLE, get_rain(msg),
            "data",         "Raw data",   DATA_STRING,  msg_hex,
            "mic",          "Integrity",  DATA_STRING,  "CHECKSUM",
            NULL);

    decoder_output_data(decoder, data);

    // Return 1 if message successfully decoded
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "temperature_C",
    "humidity",
    "direction_str",
    "direction_deg",
    "speed",
    "gust",
    "rain",
    "data",
    NULL
};

r_device bresser_5in1 = {
    .name          = "Bresser Weather Center 5-in-1",
    .modulation    = FSK_PULSE_PCM,
    .short_width   = 58,
    .long_width    = 58,
    .reset_limit   = 1500,
    .decode_fn     = &bresser_5in1_callback,
    .disabled      = 1,
    .fields        = output_fields,
};
