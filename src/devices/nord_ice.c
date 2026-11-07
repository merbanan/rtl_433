/** @file
    Nord ICE gate remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Nord ICE gate remotes.

Seen on 433.92 MHz. PWM coded, a 1 bit is a 800 us pulse and a 300 us gap, a 0 bit is a
300 us pulse and a 800 us gap. A message is 33 bits, repeated after a 7.5 ms gap.

Data layout (bit 32 sent first):

- bits 32..15: serial, high 18 bits
- bits 14..9: 6 bit button
- bits 8..0: serial, low 9 bits

There is no integrity check, so two identical messages of exactly 33 bits are required,
all 0 and all 1 codes are rejected. Silvercrest remotes (`silvercrest.c`) use the same framing,
messages that pass the Silvercrest header and command check are rejected.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/nord_ice.c`.

Flex decoder (raw code only):

    rtl_433 -X 'n=nord_ice,m=OOK_PWM,s=300,l=800,g=2000,r=12000,t=200,invert,bits=33,repeats>=2'
*/

static int nord_ice_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    int row = -1;
    for (int i = 0; i < bitbuffer->num_rows && row < 0; ++i) {
        if (bitbuffer->bits_per_row[i] != 33) {
            continue;
        }
        if (bitbuffer_count_repeats(bitbuffer, i, 0) >= 2) {
            row = i;
        }
    }
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t b[5] = {0};
    bitbuffer_extract_bytes(bitbuffer, row, 0, b, 33);
    uint64_t data = ((uint64_t)b[0] << 25) | ((uint64_t)b[1] << 17) | (b[2] << 9) | (b[3] << 1) | (b[4] >> 7);

    // Reject all 0 and all 1 codes
    if (data == 0 || data == 0x1ffffffffULL) {
        return DECODE_FAIL_SANITY;
    }

    // Exclude Silvercrest remotes (silvercrest.c): same framing, header 0x7c26 and checked command, not inverted
    uint8_t const silvercrest_tab[16] = {2, 3, 0, 1, 4, 5, 7, 6, 0xc, 0xd, 0xf, 0xe, 8, 9, 0xb, 0xa};
    if (b[0] == 0x83 && b[1] == 0xd9 && ((~b[3]) & 0x0f) == silvercrest_tab[(~b[2]) & 0x0f]) {
        return DECODE_FAIL_SANITY;
    }

    int id     = (int)(((data >> 15) << 9) | (data & 0x1ff));
    int button = (data >> 9) & 0x3f;

    /* clang-format off */
    data_t *output = data_make(
            "model",    "",         DATA_STRING, "NordICE",
            "id",       "",         DATA_FORMAT, "%07x", DATA_INT, id,
            "button",   "Button",   DATA_FORMAT, "%02x", DATA_INT, button,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, output);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        NULL,
};

r_device const nord_ice = {
        .name        = "Nord ICE gate remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 300,
        .long_width  = 800,
        .gap_limit   = 2000,
        .reset_limit = 12000,
        .tolerance   = 200,
        .decode_fn   = &nord_ice_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields,
};
