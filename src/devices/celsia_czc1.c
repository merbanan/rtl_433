/** @file
    Celsia CZC1 Thermostat.

    Copyright (C) 2023 Liban Hannan <liban.p@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Celsia CZC1 Thermostat.

A PID thermostat compatible with various manufacturers' heaters.

demod: OOK_PCM
short: 1220
long: 1220
reset: 4880

A packet starts with a preamble of {40}cccccccccccccccccccc, followed by a sync
of {32}55555555 signalling the start of the data symbols. The packet is
terminated with {8}f0.  Each symbol is 4 'raw' bits long: 0101(5) = 0, 1010(a)
= 1. Command packets have 5 bytes of data, pairing packets have 4.

```
rtl_433 -X n=CZC1,m=OOK_PCM,s=1220,l=1220,r=4880,preamble=cccccccc55555555
```

Data layout:

Command packet (5 bytes)

- ID:   {16} ID
- Type: {8}  type
- Heat: {8}  heating level 0-255 (bit reflected unsigned integer)
- CRC:  {8}  CRC-8, poly 0x31, init 0xd7

Pairing packet (4 bytes)

- ID:   {16} ID
- Type: {8}  type
- CRC:  {8}  CRC-8, poly 0x31, init 0xd7

*/

static int celsia_czc1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xcc, 0xcc, 0xcc, 0xcc, 0x55, 0x55, 0x55, 0x55};
    // data section in command packet == 160 bits
    // data section in pair packet == 128 bits
    // terminal 0xf == 4 bits

    if (bitbuffer->num_rows > 1 || bitbuffer->bits_per_row[0] < 144) {
        return DECODE_ABORT_EARLY;
    }

    unsigned preamble_end = bitbuffer_search(bitbuffer, 0, 0, preamble, 64) + 64;
    unsigned first_byte = preamble_end >> 3;

    if (preamble_end >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }

    if ((preamble_end + 132) > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_t decoded_bits = {0};
    //convert raw bits to symbols

    uint8_t *bits = bitbuffer->bb[0];
    unsigned int n_bytes = bitbuffer->bits_per_row[0] >> 3;
    unsigned int ipos = first_byte;
    while (ipos < n_bytes) {
        if (bits[ipos] == 0xf0) {
            break;
        }
        switch (bits[ipos]) {
        case 0x55:
            bitbuffer_add_bit(&decoded_bits, 0);
            bitbuffer_add_bit(&decoded_bits, 0);
            break;
        case 0x5a:
            bitbuffer_add_bit(&decoded_bits, 0);
            bitbuffer_add_bit(&decoded_bits, 1);
            break;
        case 0xa5:
            bitbuffer_add_bit(&decoded_bits, 1);
            bitbuffer_add_bit(&decoded_bits, 0);
            break;
        case 0xaa:
            bitbuffer_add_bit(&decoded_bits, 1);
            bitbuffer_add_bit(&decoded_bits, 1);
            break;
        }
        ipos++;
    }

    decoder_log_bitbuffer(decoder, 2, __func__, &decoded_bits, "Extracted data");
    uint8_t *b = decoded_bits.bb[0];

    uint8_t crc = crc8(b, 8, 0x31, 0xd7);
    if (crc != 0) {
        decoder_log(decoder, 2, __func__, "Decode failed: CRC failed");
        return DECODE_FAIL_MIC;
    }

    // Check if a 0x00 pair packet or a 0xf0 command packet
    if (b[2] != 0x00 && b[2] != 0xf0) {
        decoder_log(decoder, 1, __func__, "Unknown packet type");
        return DECODE_FAIL_OTHER;
    }

    int id      = (b[0] << 8) | b[1];
    int heat_ok = b[2] == 0xf0;   // is it a command packet?
    int heat    = reverse8(b[3]); // command packet only

    /* clang-format off */
    data_t *data = data_make(
        "model",    "",             DATA_STRING, "Celsia-CZC1",
        "id",       "",             DATA_FORMAT, "%x",    DATA_INT, id,
        "heat",     "Heat",         DATA_COND,   heat_ok, DATA_INT, heat,
        "mic",      "Integrity",    DATA_STRING, "CRC",
        NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "heat",
        "mic",
        NULL,
};

//rtl_433 -X n=CZC1,m=OOK_PCM,s=1220,l=1220,r=4880,preamble=cccccccc55555555

r_device const celsia_czc1 = {
        .name        = "Celsia CZC1 Thermostat",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 1220, // each pulse is ~1220 us (nominal bit width)
        .long_width  = 1220, // each pulse is ~1220 us (nominal bit width)
        .reset_limit = 4880, // larger than gap between start pulse and first frame (6644 us = 11 x nominal bit width) to put start pulse and first frame in two rows, but smaller than inter-frame space of 30415 us
        .tolerance   = 20,
        .decode_fn   = &celsia_czc1_decode,
        .fields      = output_fields,
};
