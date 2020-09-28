/** @file
    Decoder for Bresser Weather Center 7-in-1.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decoder for Bresser Weather Center 7-in-1, outdoor sensor.

See https://github.com/merbanan/rtl_433/issues/1492

Preamble:

    aa aa aa aa aa 2d d4

Observed length depends on reset_limit.
The data has a whitening of 0xaa.

Data layout:

    {271}631d05c09e9a18abaabaaaaaaaaa8adacbacff9cafcaaaaaaa000000000000000000

    DIGEST:8h8h ID?8h8h WDIR:8h4h 4h 8h WGUST:8h.4h WAVG:8h.4h RAIN:8h8h4h.4h RAIN?:8h TEMP:8h.4hC 4h HUM:8h% LIGHT:8h4h,4hKL ?:8h8h4h TRAILER:8h8h8h4h

Unit of light is kLux (not W/m²).

First two bytes are an LFSR-16 digest, generator 0x8810 key 0xba95 with a final xor 0x6df1, which likely means we got that wrong.
*/

static int bresser_7in1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0xaa, 0x2d, 0xd4};

    data_t *data;
    uint8_t msg[25];

    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] < 240-80) {
        if (decoder->verbose > 1)
            fprintf(stderr, "%s: to few bits (%u)\n", __func__, bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH; // unrecognized
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);
    start_pos += sizeof(preamble_pattern) * 8;

    if (start_pos >= bitbuffer->bits_per_row[0]) {
        if (decoder->verbose > 1)
            fprintf(stderr, "%s: preamble not found\n", __func__);
        return DECODE_ABORT_EARLY; // no preamble found
    }
    //if (start_pos + sizeof (msg) * 8 >= bitbuffer->bits_per_row[0]) {
    if (start_pos + 21*8 >= bitbuffer->bits_per_row[0]) {
        if (decoder->verbose > 1)
            fprintf(stderr, "%s: message too short (%u)\n", __func__, bitbuffer->bits_per_row[0] - start_pos);
        return DECODE_ABORT_LENGTH; // message too short
    }

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, sizeof (msg) * 8);
    if (decoder->verbose > 1) {
        bitrow_printf(msg, sizeof(msg) * 8, "%s: MSG: ", __func__);
    }

    if (msg[21] == 0x00) {
        return DECODE_FAIL_SANITY;
    }
    // data whitening
    for (unsigned i = 0; i < sizeof (msg); ++i) {
        msg[i] ^= 0xaa;
    }
    if (decoder->verbose > 1) {
        bitrow_printf(msg, sizeof(msg) * 8, "%s: XOR: ", __func__);
    }

    // LFSR-16 digest, generator 0x8810 key 0xba95 final xor 0x6df1
    int chk    = (msg[0] << 8) | msg[1];
    int digest = lfsr_digest16(&msg[2], 23, 0x8810, 0xba95);
    if ((chk ^ digest) != 0x6df1) {
        if (decoder->verbose > 1)
            fprintf(stderr, "%s: Digest check failed %04x vs %04x (%04x)\n", __func__, chk, digest, chk ^ digest);
        return DECODE_FAIL_MIC;
    }

    int id       = (msg[2] << 8) | (msg[3]);
    int wdir     = (msg[4] >> 4) * 100 + (msg[4] & 0x0f) * 10 + (msg[5] >> 4);
    int wgst_raw = (msg[7] >> 4) * 100 + (msg[7] & 0x0f) * 10 + (msg[8] >> 4);
    int wavg_raw = (msg[8] & 0x0f) * 100 + (msg[9] >> 4) * 10 + (msg[9] & 0x0f);
    int rain_raw = (msg[10] >> 4) * 100000 + (msg[10] & 0x0f) * 10000 + (msg[11] >> 4) * 1000
            + (msg[11] & 0x0f) * 100 + (msg[12] >> 4) * 10 + (msg[12] & 0x0f) * 1; // 6 digits
    float rain_mm = rain_raw * 0.1f;
    int temp_raw = (msg[14] >> 4) * 100 + (msg[14] & 0x0f) * 10 + (msg[15] >> 4);
    float temp_c = temp_raw * 0.1f;
    if (temp_raw > 600)
        temp_c = (temp_raw - 1000) * 0.1f;
    int humidity = (msg[16] >> 4) * 10 + (msg[16] & 0x0f);
    int lght_raw = (msg[17] >> 4) * 1000 + (msg[17] & 0x0f) * 100 + (msg[18] >> 4) * 10 + (msg[18] & 0x0f);

    float light_klx = lght_raw * 0.1f;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Bresser-7in1",
            "id",               "",             DATA_INT,    id,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_INT,    humidity,
            "wind_max_m_s",     "Wind Gust",    DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wgst_raw * 0.1f,
            "wind_avg_m_s",     "Wind Speed",   DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wavg_raw * 0.1f,
            "wind_dir_deg",     "Direction",    DATA_INT,    wdir,
            "rain_mm",          "Rain",         DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_mm,
            "light_klx",        "Light",        DATA_FORMAT, "%.1f klx", DATA_DOUBLE, light_klx,
            "mic",              "Integrity",    DATA_STRING, "CRC",
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
        "wind_max_m_s",
        "wind_avg_m_s",
        "wind_dir_deg",
        "rain_mm",
        "light_klx",
        "mic",
        NULL,
};

r_device bresser_7in1 = {
        .name        = "Bresser Weather Center 7-in-1",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 124,
        .long_width  = 124,
        .reset_limit = 25000,
        .decode_fn   = &bresser_7in1_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
