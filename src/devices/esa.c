/** ELV Energy Counter ESA 1000/2000.
 *
 * Copyright (C) 2016 TylerDurden23, initial cleanup by Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

#define MAXMSG 40               // ESA messages

static uint8_t decrypt_esa(uint8_t *b)
{
    uint8_t pos = 0;
    uint8_t i = 0;
    uint8_t salt = 0x89;
    uint16_t crc = 0xf00f;
    uint8_t byte;

    for (i = 0; i < 15; i++) {
        byte = b[pos];
        crc += byte;
        b[pos++] ^= salt;
        salt = byte + 0x24;
    }
    byte = b[pos];
    crc += byte;
    b[pos++] ^= 0xff;

    crc -= (b[pos] << 8) | b[pos+1];
    return crc;
}

static int esa_cost_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t b[MAXMSG];

    unsigned is_retry, sequence_id, deviceid, impulses;
    unsigned impulse_constant, impulses_val, impulses_total;
    float energy_total_val, energy_impulse_val;

    if (bitbuffer->bits_per_row[0] != 160 || bitbuffer->num_rows != 1)
        return 0;

    // remove first two bytes?
    bitbuffer_extract_bytes(bitbuffer, 0, 16, b, 160 - 16);

    if (decrypt_esa(b))
        return 0; // checksum fail

    is_retry           = (b[0] >> 7);
    sequence_id        = (b[0] & 0x7f);
    deviceid           = (b[1]);
    impulses           = (b[3] << 8) | b[4];
    impulse_constant   = ((b[14] << 8) | b[15]) ^ b[1];
    impulses_total     = ((unsigned)b[5] << 24) | (b[6] << 16) | (b[7] << 8) | b[8];
    impulses_val       = (b[9] << 8) | b[10];
    energy_total_val   = 1.0 * impulses_total / impulse_constant;
    energy_impulse_val = 1.0 * impulses_val / impulse_constant;

    data = data_make(
            "model",            "Model",            DATA_STRING, "ESA-x000",
            "id",               "Id",               DATA_INT, deviceid,
            "impulses",         "Impulses",          DATA_INT, impulses,
            "impulses_total",   "Impulses Total",   DATA_INT, impulses_total,
            "impulse_constant", "Impulse Constant", DATA_INT, impulse_constant,
            "total_kWh",        "Energy Total",     DATA_DOUBLE, energy_total_val,
            "impulse_kWh",      "Energy Impulse",   DATA_DOUBLE, energy_impulse_val,
            "sequence_id",      "Sequence ID",      DATA_INT, sequence_id,
            "is_retry",         "Is Retry",         DATA_INT, is_retry,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "impulses",
    "impulses_total",
    "impulse_constant",
    "total_kWh",
    "impulse_kWh",
    "sequence_id",
    "is_retry",
    "mic",
    NULL
};

r_device esa = {
    .name           = "ESA1000 / ESA2000 Energy Monitor",
    .modulation     = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_width    = 260,
    .long_width     = 0,
    .reset_limit    = 3000,
    .decode_fn      = &esa_cost_callback,
    .disabled       = 1,
    .fields         = output_fields,
};
