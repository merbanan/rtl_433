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

Nice One remotes repeat the Nice Flor-s protocol with 20 additional bytes:
a packet is made of 72 bits
*/

#include "decoder.h"

static const uint8_t leaf_node[] = {
    25, 5, 63, 97, 203, 109, 69, 10, 3, 7, 64, 5, 71, 134, 180, 74,
    41, 158, 102, 199, 93, 118, 175, 101, 60, 77, 143, 174, 103, 148, 29, 85
};

static void xor_array(uint8_t *p, uint8_t k)
{
    for (int i = 1; i < 6; i++) {
        p[i] ^= k;
    }
}

static uint16_t pl_reverse(uint8_t *p)
{
    uint8_t k = 0;

    k = ~p[4];
    p[5] = ~p[5];
    p[4] = ~p[2];
    p[2] = ~p[0];
    p[0] = k;
    k = ~p[3];
    p[3] = ~p[1];
    p[1] = k;

    for (uint8_t y = 0; y < 2; y++) {
        k = leaf_node[p[0] >> 3] + 0x25;
        xor_array(p, k);
        p[5] &= 0x0f;
        p[0] ^= k & 0x7;
        k = leaf_node[p[0] & 0x1f];
        xor_array(p, k);
        p[5] &= 0x0f;
        p[0] ^= k & 0xe0;
        if (y == 0) {
            k = p[0];
            p[0] = p[1];
            p[1] = k;
        }
    }
    return ((uint16_t)p[1] << 8) | p[0];
}

static int nice_flor_s_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b = bitbuffer->bb[0];
    uint8_t t_buf[7] = {0};
    uint8_t p[7] = {0};

    if (bitbuffer->num_rows != 2 || bitbuffer->bits_per_row[1] != 0) {
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[0] != 52 && bitbuffer->bits_per_row[0] != 72) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_invert(bitbuffer);

    decoder_logf_bitbuffer(decoder, 1, __func__, bitbuffer, "nice_flor_s:");

    t_buf[0] = (b[0] >> 4) & 0x0f;
    for (int i = 0; i < 6; i++) {
        t_buf[i+1] = ((b[i] << 4) & 0xf0) | ((b[i+1] >> 4) & 0x0f);
    }

    p[5] = t_buf[1] & 0x0f;
    p[4] = t_buf[2];
    p[3] = t_buf[3];
    p[2] = t_buf[4];
    p[1] = t_buf[5];
    p[0] = t_buf[6];

    uint16_t code = pl_reverse(p);

    uint32_t serial = ((uint32_t)p[5] << 24) | (p[4] << 16) | (p[3] << 8) | p[2];
    uint8_t button_id = t_buf[0] & 0x0f;
    uint8_t count = ((t_buf[1] >> 4) & 0x0f) ^ (t_buf[0] & 0x0f) ^ 0x0f;

    /* clang-format off */
    data_t *data = data_make(
            "model",  "",              DATA_STRING, "Nice-FlorS",
            "button", "Button ID",     DATA_INT,     button_id,
            "serial", "Serial",        DATA_FORMAT,  "%07x", DATA_INT, serial,
            "code",   "Code",          DATA_FORMAT,  "%04x", DATA_INT, code,
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
