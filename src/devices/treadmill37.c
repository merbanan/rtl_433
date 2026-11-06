/** @file
    Treadmill remotes, 37 bit.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Treadmill remotes, 37 bit.

Seen on 433.92 MHz. PWM coded, a 1 bit is a 900 us pulse and a 300 us gap, a 0 bit is a
300 us pulse and a 900 us gap. A message is 37 bits, repeated after a 6 ms gap.

Data layout:

    SSSSS BBBB X

- S: 20 bit serial
- B: 16 bit button code
- X: 1 bit

There is no integrity check, so two identical messages of exactly 37 bits are required,
all 0 and all 1 serials are rejected.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/treadmill37.c`.

Flex decoder (without the length and serial checks):

    rtl_433 -X 'n=treadmill37,m=OOK_PWM,s=300,l=900,g=2000,r=10000,t=250,invert,bits=37,repeats>=2'
*/

static int treadmill37_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    int row = -1;
    for (int i = 0; i < bitbuffer->num_rows && row < 0; ++i) {
        if (bitbuffer->bits_per_row[i] != 37) {
            continue;
        }
        if (bitbuffer_count_repeats(bitbuffer, i, 0) >= 2) {
            row = i;
        }
    }
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *b = bitbuffer->bb[row];

    int id     = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);
    int button = ((b[2] & 0x0f) << 12) | (b[3] << 4) | (b[4] >> 4);

    // Reject all 0 and all 1 serials
    if (id == 0x00000 || id == 0xfffff) {
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",         DATA_STRING, "Treadmill37",
            "id",       "",         DATA_FORMAT, "%05x", DATA_INT, id,
            "button",   "Button",   DATA_FORMAT, "%04x", DATA_INT, button,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        NULL,
};

r_device const treadmill37 = {
        .name        = "Treadmill remotes, 37 bit",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 300,
        .long_width  = 900,
        .gap_limit   = 2000,
        .reset_limit = 10000,
        .tolerance   = 250,
        .decode_fn   = &treadmill37_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields,
};
