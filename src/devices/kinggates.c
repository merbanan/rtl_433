/** @file
    King Gates Stylo 4k gate remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
King Gates Stylo 4k rolling code gate remotes.

The hop code is KeeLoq encrypted (simple learning), but the message format is not the one of
the Microchip HCS encoders (see the Microchip-HCS200 decoder).

Seen on 433.92 MHz. A message is a 9.5 ms gap, 12 x (400 us pulse, 400 us gap), a 2.2 ms gap,
an 800 us start pulse, then 89 bits. Each bit is a gap followed by a pulse, a 1 bit is a
400 us gap and a 1100 us pulse, a 0 bit is a 1100 us gap and a 400 us pulse.
The message is repeated.

Data layout, sent LSB first, fix part then hop part:

    SSSSSSSSSSSSSSSS 1 BBBB SSSSSSSSSSSSSSSS 0000000000000000 HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH 0000

- S: 32 bit serial, low 16 bits first, then the high 16 bits
- 1: always 1
- B: 4 bit button, 0xe, 0xd, 0xb or 0x7
- 0: always 0
- H: 32 bit hop code, KeeLoq encrypted (button, 0xc, low serial byte, 16 bit counter)

There is no integrity check, the constant bits and two identical messages are required.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/kinggates_stylo_4k.c`.

Flex decoder:

    rtl_433 -X 'n=kinggates,m=OOK_PWM,s=400,l=1100,g=2000,r=12000,t=200,invert,repeats>=2,bits=89'
*/

static int kinggates_stylo4k_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 89);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[row] != 89) {
        return DECODE_ABORT_LENGTH;
    }

    // Fix part, 53 bits LSB first
    uint64_t fix = 0;
    for (int i = 0; i < 53; ++i) {
        fix |= (uint64_t)bitrow_get_bit(bitbuffer->bb[row], i) << i;
    }
    // Hop part, 32 bits LSB first, then 4 zero bits
    uint32_t hop = 0;
    for (int i = 0; i < 32; ++i) {
        hop |= (uint32_t)bitrow_get_bit(bitbuffer->bb[row], 53 + i) << i;
    }
    int hop_end = 0;
    for (int i = 85; i < 89; ++i) {
        hop_end |= bitrow_get_bit(bitbuffer->bb[row], i);
    }

    // Constant bits
    if (!((fix >> 16) & 1) || (fix >> 37) != 0 || hop_end != 0) {
        return DECODE_FAIL_SANITY;
    }

    uint32_t serial = (uint32_t)(((fix >> 21) & 0xffff) << 16) | (fix & 0xffff);
    int button      = (fix >> 17) & 0xf;

    char serial_str[9];
    snprintf(serial_str, sizeof(serial_str), "%08X", serial);
    char encrypted_str[9];
    snprintf(encrypted_str, sizeof(encrypted_str), "%08X", hop);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",         DATA_STRING, "KingGates-Stylo4k",
            "id",           "",         DATA_STRING, serial_str,
            "button",       "Button",   DATA_FORMAT, "%x", DATA_INT, button,
            "encrypted",    "",         DATA_STRING, encrypted_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        "encrypted",
        NULL,
};

r_device const kinggates_stylo4k = {
        .name        = "King Gates Stylo 4k gate remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 400,
        .long_width  = 1100,
        .gap_limit   = 2000,  // above the 1100 us bit gap
        .reset_limit = 12000, // above the 9.5 ms message gap
        .tolerance   = 200,
        .decode_fn   = &kinggates_stylo4k_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields,
};
