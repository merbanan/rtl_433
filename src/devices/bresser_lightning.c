/** @file
    Bresser Lightning Sensor.

    Copyright (C) 2023 The rtl_433 Project

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define SENSOR_TYPE_LIGHTNING 9

/**
Bresser Lightning Sensor.

Decoder for Bresser lightning outdoor sensor, PN 7009976

see https://github.com/merbanan/rtl_433/issues/2140

Preamble: aa aa 2d d4

Data layout:
    DIGEST:8h8h ID:8h8h CTR:12h BATT:1b ?3b STYPE:4h STARTUP:1b CH:3d KM:8d ?8h8h

Based on bresser_7in1.c

The data (not including STYPE, STARTUP, CH and maybe ID) has a whitening of 0xaa.
CH is always 0.

First two bytes are an LFSR-16 digest, generator 0x8810 key 0xabf9 with a final xor 0x899e
*/

static int bresser_lightning_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0x2d, 0xd4};
    uint8_t msg[25];

    /* clang-format off */
    if (   bitbuffer->num_rows != 1
        || bitbuffer->bits_per_row[0] < 160
        || bitbuffer->bits_per_row[0] > 440) {
        decoder_logf(decoder, 2, __func__, "bit_per_row %u out of range", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_EARLY; // Unrecognized data
    }
    /* clang-format on */

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);

    if (start_pos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }
    start_pos += sizeof(preamble_pattern) * 8;

    unsigned len = bitbuffer->bits_per_row[0] - start_pos;
    if (len < sizeof(msg) * 8) {
        decoder_logf(decoder, 2, __func__, "%u too short", len);
        return DECODE_ABORT_LENGTH; // message too short
    }

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, sizeof(msg) * 8);

    decoder_log_bitrow(decoder, 2, __func__, msg, sizeof(msg) * 8, "MSG");

    int s_type      = msg[6] >> 4;
    int chan        = msg[6] & 0x07;
    int battery_low = (msg[5] & 0x08) >> 3;
    int nstartup    = (msg[6] & 0x08) >> 3;

    // data de-whitening
    for (unsigned i = 0; i < sizeof(msg); ++i) {
        msg[i] ^= 0xaa;
    }
    decoder_log_bitrow(decoder, 2, __func__, msg, sizeof(msg) * 8, "XOR");

    // LFSR-16 digest, generator 0x8810 key 0xba95 final xor 0x6df1
    int chk    = (msg[0] << 8) | msg[1];
    int digest = lfsr_digest16(&msg[2], 23, 0x8810, 0xba95);
    if ((chk ^ digest) != 0x6df1) {
        decoder_logf(decoder, 2, __func__, "Digest check failed %04x vs %04x (%04x)", chk, digest, chk ^ digest);
        return DECODE_FAIL_MIC;
    }

    int sensor_id   = (msg[2] << 8) | (msg[3]);
    int distance_km = msg[7];
    int count       = (msg[4] << 4) | (msg[5] & 0xf0) >> 4;
    int unknown1    = ((msg[5] & 0x0f) << 8) | msg[6];
    int unknown2    = (msg[8] << 8) | msg[9];

    // Sanity checks
    if ((s_type != SENSOR_TYPE_LIGHTNING) || (chan != 0)) {
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                     DATA_STRING, "Bresser-Lightning",
            "id",               "",                     DATA_FORMAT, "%08x",      DATA_INT,    sensor_id,
            "startup",          "Startup",              DATA_COND,   !nstartup,   DATA_INT,    !nstartup,
            "battery_ok",       "Battery",              DATA_INT,    !battery_low,
            "distance_km",      "storm_distance_km",    DATA_INT,    distance_km,
            "strike_count",     "strike_count",         DATA_INT,    count,
            "unknown1",         "Unknown1",             DATA_FORMAT, "%08x",      DATA_INT,    unknown1,
            "unknown2",         "Unknown2",             DATA_FORMAT, "%08x",      DATA_INT,    unknown2,
            "mic",              "Integrity",            DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "startup",
        "battery_ok",
        "distance_km",
        "strike_count",
        "unknown1",
        "unknown2",
        NULL,
};

r_device const bresser_lightning = {
        .name        = "Bresser lightning",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 124,
        .long_width  = 124,
        .reset_limit = 25000,
        .decode_fn   = &bresser_lightning_decode,
        .fields      = output_fields,
};
