/**
 * Copyright (C) 2016 TylerDurden23, initial cleanup by Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* TODO: Replace the helper functions with native code */


#include "decoder.h"

#define _BV(bit) (1 << (bit))
#define MAXMSG 40               // ESA messages

typedef struct  {
    uint8_t *data;
    uint8_t byte, bit;
} input_t;

// Helper functions
uint8_t getbit(input_t *in)
{
    uint8_t bit = (in->data[in->byte] & _BV(in->bit)) ? 1 : 0;
    if(in->bit-- == 0) {
        in->byte++;
        in->bit=7;
    }
    return bit;
}

uint8_t getpacket_bits(input_t* in, uint8_t npacket_bits, uint8_t msb)
{
    uint8_t ret = 0, i;
    for (i = 0; i < npacket_bits; i++) {
        if (getbit(in) )
            ret = ret | _BV( msb ? npacket_bits-i-1 : i );
    }
    return ret;
}

uint8_t analyze_esa(uint8_t *b, uint8_t *obuf)
{
    input_t in;
    in.byte = 0;
    in.bit = 7;
    in.data = b;

    uint8_t oby = 0;
    uint8_t salt = 0x89;
    uint16_t crc = 0xf00f;

    for (oby = 0; oby < 15; oby++) {
        uint8_t byte = getpacket_bits(&in, 8, 1);
        crc += byte;
        obuf[oby] = byte ^ salt;
        salt = byte + 0x24;
    }
    obuf[oby] = getpacket_bits(&in, 8, 1);
    crc += obuf[oby];
    obuf[oby++] ^= 0xff;

    crc -= (getpacket_bits(&in, 8, 1)<<8);
    crc -= getpacket_bits(&in, 8, 1);

    if (crc)
        return 0;

    return 1;
}

//done helpers

static int esa_cost_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t packet[MAXMSG];
    uint8_t obuf[MAXMSG]; // parity-stripped output
    unsigned impulse_constant_val, impulses_val, impulses_total_val;
    float energy_total_val, energy_impulse_val;

    if (bitbuffer->bits_per_row[0] != 160 || bitbuffer->num_rows != 1) 
        return 0;

    // remove first two bytes?
    bitbuffer_extract_bytes(bitbuffer, 0, 16, packet, 160 - 16);

    if(!analyze_esa(packet, obuf)) 
        return 0;   // checksum fail

    impulse_constant_val = (((uint16_t)obuf[14] << 8) |obuf[15]) ^ obuf[1];
    impulses_total_val = ((uint32_t)obuf[5] << 24) | ((uint32_t)obuf[6] << 16) | ((uint32_t)obuf[7] << 8) |obuf[8];
    impulses_val = ((uint16_t)obuf[9] << 8) |obuf[10];
    energy_total_val = 1.0 * impulses_total_val / impulse_constant_val;
    energy_impulse_val = 1.0 * impulses_val / impulse_constant_val;

    uint8_t deviceid = obuf[1];
    uint8_t sequence_id = (obuf[0] << 1) >> 1;
    uint16_t impulses = (obuf[3] << 8) | obuf[4];
    uint32_t impulses_total = (obuf[5] << 24) | (obuf[6] << 16) | (obuf[7] << 8) | obuf[8];
    uint16_t impulse_constant = ((obuf[14] << 8) |obuf[15]) ^ obuf[1];
    uint8_t is_retry = ((obuf[0])>>7);

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
