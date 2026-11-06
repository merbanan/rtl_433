/** @file
    Hay21 remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Hay21 remotes.

Seen on 433.92 MHz. PWM coded, a 1 bit is a 700 us pulse and a 300 us gap, a 0 bit is a
300 us pulse and a 700 us gap. A message is 21 bits, repeated after a 4.2 ms gap.

Data layout:

    BBBBBBBB SSSSSSSS CCCC 1

- B: 8 bit button, e.g. 0x5a on/off, 0xc3 mode, 0x88 hold
- S: 8 bit serial
- C: 4 bit counter, counts up on each press
- 1: 1 bit, always 1

There is no integrity check, so two identical messages of exactly 21 bits are required.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/hay21.c`.

Flex decoder (without the length and end bit checks):

    rtl_433 -X 'n=hay21,m=OOK_PWM,s=300,l=700,g=2000,r=10000,t=200,invert,bits=21,repeats>=2'
*/

static int hay21_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    int row = -1;
    for (int i = 0; i < bitbuffer->num_rows && row < 0; ++i) {
        if (bitbuffer->bits_per_row[i] != 21) {
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

    // End bit, always 1
    if (!(b[2] & 0x08)) {
        return DECODE_FAIL_SANITY;
    }
    // Reject all 0 and all 1 codes
    if ((b[0] == 0x00 && b[1] == 0x00) || (b[0] == 0xff && b[1] == 0xff)) {
        return DECODE_FAIL_SANITY;
    }

    int button  = b[0];
    int id      = b[1];
    int counter = b[2] >> 4;

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",         DATA_STRING, "Hay21",
            "id",       "",         DATA_FORMAT, "%02x", DATA_INT, id,
            "button",   "Button",   DATA_FORMAT, "%02x", DATA_INT, button,
            "counter",  "Counter",  DATA_INT,    counter,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        "counter",
        NULL,
};

r_device const hay21 = {
        .name        = "Hay21 remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 300,
        .long_width  = 700,
        .gap_limit   = 2000,
        .reset_limit = 10000,
        .tolerance   = 200,
        .decode_fn   = &hay21_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields,
};
