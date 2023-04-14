/** @file
    Nice Flor-s remote for gates.

    Copyright (C) 2020 Samuel Tardieu <sam@rfc1149.net>

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

    uint8_t button_id = b[0] >> 4;
    if (button_id < 1 || button_id > 4) {
        return DECODE_ABORT_EARLY;
    }
    int count = 1 + (((b[0] ^ ~button_id) - 1) & 0xf);
    uint32_t serial = ((b[1] & 0xf0) << 20) | ((b[3] & 0xf) << 20) |
       (b[4] << 12) | (b[5] << 4) | (b[6] >> 4);
    uint16_t code = (b[1] << 12) | (b[2] << 4) | (b[3] >> 4);

    /* clang-format off */
    data_t *data = data_make(
            "model",  "",              DATA_STRING, "Nice-FlorS",
            "button", "Button ID",     DATA_INT,     button_id,
            "serial", "Serial (enc.)", DATA_FORMAT, "%07x",        DATA_INT, serial,
            "code",   "Code (enc.)",   DATA_FORMAT, "%04x",        DATA_INT, code,
            "count",  "",              DATA_INT,     count,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "button",
        "serial",
        "code",
        "count",
        NULL,
};

// Example:
// $ rtl_433 -R 169 -y "{52} 0xe7a760b94372e {0}"
// time      : 2020-10-21 11:06:12
// model     : Nice Flor-s  Button ID : 1             Serial (enc.): 56bc8d1    Code (enc.): 89f4
// count     : 6

r_device const nice_flor_s = {
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
