/** @file
    Nice-FlorS remote for gates.

    Copyright (C) 2022 Daniel Henzulea <zulea1@gmail.com>
    based on code Copyright (C) 2020 Samuel Tardieu <sam@rfc1149.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Nice Flor-s remote for gates.

Protocol description:
The protocol has been analyzed at this link: http://phreakerclub.com/1615

A packet is made of 52 bits (13 nibbles S0 to S12):

- S0: button ID from 1 to 4 (or 1 to 2 depending on the remote)
- S1: retransmission count starting from 1, xored with ~S0
- S2 and S7-S12: 28 bit encrypted serial number
- S3-S6: 16 bits encrypted rolling code
*/

#include "decoder.h"
#include "nice_flor_s_table_decode.h"
#include "nice_flor_s_table_ki.h"

static int nice_flor_s_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 2 || bitbuffer->bits_per_row[1] != 0) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[0] != 52) {
        return DECODE_ABORT_LENGTH;
    }
    bitbuffer_invert(bitbuffer);
    uint8_t *b = bitbuffer->bb[0];
    uint8_t encbuff[7];
    encbuff[0] = (b[0] >> 4) & 0x0f;
    for (uint8_t i = 0; i < 6; i++) {
        encbuff[i+1] = ((b[i] << 4) & 0xf0) | ((b[i+1] >> 4) & 0x0f);
    }
    uint16_t enccode = ((encbuff[2] << 8) & 0xff00) | (encbuff[3] & 0x00ff);
    uint16_t deccode = nice_flor_s_table_decode[enccode];
    uint8_t ki = nice_flor_s_table_ki[deccode & 0xff] ^ (enccode & 0xff);
    uint8_t       snbuff[4];
    snbuff[3] = (encbuff[1] ^ ki) & 0x0f;
    snbuff[2] = encbuff[4] ^ ki;
    snbuff[1] = encbuff[5] ^ ki;
    snbuff[0] = encbuff[6] ^ ki;
    uint32_t serial = (uint32_t)((snbuff[3] << 24) & 0xff000000) | ((snbuff[2] << 16) & 0xff0000) | ((snbuff[1] << 8) & 0xff00) | (snbuff[0] & 0xff);
    uint16_t code = deccode;
    uint8_t button_id = encbuff[0] & 0x0f;
    uint8_t repeat = ((encbuff[1] >> 4) & 0x0f) ^ (encbuff[0] & 0x0f) ^ 0x0f;

    /* clang-format off */
    data_t *data = data_make(
            "model",  "",              DATA_STRING, "Nice-FlorS",
            "button", "Button ID",     DATA_INT,     button_id,
            "serial", "Serial (hex)",  DATA_FORMAT, "0x%07x", DATA_INT, serial,
            "code",   "Code (dec)",    DATA_FORMAT, "%d", DATA_INT, code,
            "count",  "Repeat counter",DATA_INT,     repeat,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "button",
        "serial",
        "code",
        "count",
        NULL,
};

// Example:
// $ rtl_433 -R 169 -y "{52} 0xd6f703d160ad9 {0}"
// time      : 2022-11-17 18:30:20
// model     : Nice-FlorS  Button ID : 2             Serial    : 0x3aab665     Code (crt idx): 2813
// Repeat counter: 4

r_device nice_flor_s = {
        .name        = "Nice Flor-s remote control for gates",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 500,  // short pulse is ~500 us + ~1000 us gap
        .long_width  = 1000, // long pulse is ~1000 us + ~500 us gap
        .sync_width  = 1500, // sync pulse is ~1500 us + ~1500 us gap
        .gap_limit   = 2000,
        .reset_limit = 5000,
        .tolerance   = 100,
        .decode_fn   = &nice_flor_s_decode,
        .disabled    = 1,
        .fields      = output_fields,
};
