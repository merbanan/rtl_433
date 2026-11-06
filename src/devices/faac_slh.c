/** @file
    FAAC SLH rolling code gate remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
FAAC SLH rolling code gate remotes, e.g. FAAC XT2/XT4 SLH.

Seen on 433.92 MHz and 868.35 MHz. Each message is a 1190 us pulse and a 1190 us gap,
then 64 bits PWM coded: a 1 bit is a 595 us pulse and a 255 us gap, a 0 bit is a 255 us
pulse and a 595 us gap. Messages are repeated back to back while the button is pressed.

Data layout:

    FFFFFFFF HHHHHHHH

- F: 32 bit fixed part, 28 bit serial and 4 bit button
- H: 32 bit encrypted part (KeeLoq with a key derived from a per remote seed)

A master remote in programming mode sends the seed instead: the fixed part starts with
0x520f and the low byte of the encrypted part is 0. Then the 32 bit word made of the low
16 bits of the fixed part and the high 16 bits of the encrypted part is rotated right by
the low nibble of byte 1 of the encrypted part, and each byte XOR byte 1 is the seed.

There is no integrity check, at least two identical repeats are required.

Flex decoder:

    rtl_433 -X 'n=faac_slh,m=OOK_PWM,s=255,l=595,r=3000,t=100,bits=64,invert,repeats>=2'
*/

static int faac_slh_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 64);
    if (row < 0 || bitbuffer->bits_per_row[row] != 64) {
        return DECODE_ABORT_LENGTH;
    }

    // Long pulse is 1
    bitbuffer_invert(bitbuffer);
    uint8_t *b = bitbuffer->bb[row];

    uint32_t fix = ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    uint32_t hop = ((uint32_t)b[4] << 24) | (b[5] << 16) | (b[6] << 8) | b[7];

    // Reject all zero and all one codes
    if ((fix == 0 && hop == 0) || (fix == 0xffffffff && hop == 0xffffffff)) {
        return DECODE_FAIL_SANITY;
    }

    data_t *data;
    if ((fix >> 16) == 0x520f && (hop & 0xff) == 0) {
        // Programming mode, the message carries the seed
        int prog_cnt  = (hop >> 8) & 0xff;
        uint32_t word = ((fix & 0xffff) << 16) | (hop >> 16);
        int rot       = prog_cnt & 0x0f;
        if (rot) {
            word = (word >> rot) | (word << (32 - rot));
        }
        word ^= (uint32_t)prog_cnt * 0x01010101;

        char seed_str[9];
        snprintf(seed_str, sizeof(seed_str), "%08X", word);

        /* clang-format off */
        data = data_make(
                "model",    "",             DATA_STRING, "FAAC-SLH",
                "subtype",  "",             DATA_STRING, "Programming",
                "seed",     "Seed",         DATA_STRING, seed_str,
                "counter",  "Counter",      DATA_INT,    prog_cnt,
                NULL);
        /* clang-format on */
    }
    else {
        char id_str[8];
        snprintf(id_str, sizeof(id_str), "%07X", fix >> 4);
        char hop_str[9];
        snprintf(hop_str, sizeof(hop_str), "%08X", hop);

        /* clang-format off */
        data = data_make(
                "model",        "",             DATA_STRING, "FAAC-SLH",
                "id",           "",             DATA_STRING, id_str,
                "button",       "Button",       DATA_INT,    fix & 0x0f,
                "encrypted",    "",             DATA_STRING, hop_str,
                NULL);
        /* clang-format on */
    }

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "subtype",
        "id",
        "button",
        "encrypted",
        "seed",
        "counter",
        NULL,
};

r_device const faac_slh = {
        .name        = "FAAC SLH rolling code gate remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 255,
        .long_width  = 595,
        .reset_limit = 3000, // above the 1190 us header gap
        .tolerance   = 100,
        .decode_fn   = &faac_slh_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields,
};
