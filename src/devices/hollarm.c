/** @file
    Hollarm alarm system remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Hollarm alarm system remotes.

Seen on 433.92 MHz. PPM coded with 200 us pulses, a 0 bit is a 1000 us gap, a 1 bit is a
1600 us gap. A message is 42 bits, repeated after a 2.4 ms gap. The last bit is sliced as
the gap that ends the row, so a row has 41 bits.

Data layout:

    SSSSSS U B XX 00

- S: 24 bit serial
- U: 4 bit, unknown (part of the code, not shown by the Flipper Zero)
- B: 4 bit button, e.g. 0x2 arm, 0x1 disarm, 0x4 alarm, 0x8 ring
- X: 8 bit checksum, sum of the 4 bytes before
- 0: 2 bit, 0

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/hollarm.c`.

Flex decoder:

    rtl_433 -X 'n=hollarm,m=OOK_PPM,s=1000,l=1600,g=2000,r=5000,t=250,bits=41'
*/

static int hollarm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = DECODE_ABORT_LENGTH;
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned len = bitbuffer->bits_per_row[row];
        if (len != 41 && len != 42) {
            continue;
        }
        uint8_t *b = bitbuffer->bb[row];

        // Trailing 0 bits
        if (b[5] & 0x80) {
            ret = DECODE_FAIL_SANITY;
            continue;
        }
        // Validate checksum
        if ((add_bytes(b, 4) & 0xff) != b[4]) {
            ret = DECODE_FAIL_MIC;
            continue;
        }
        // Reject all 0 codes
        if (!b[0] && !b[1] && !b[2] && !b[3]) {
            ret = DECODE_FAIL_SANITY;
            continue;
        }

        int id     = (b[0] << 16) | (b[1] << 8) | b[2];
        int button = b[3] & 0x0f;

        /* clang-format off */
        data_t *data = data_make(
                "model",    "",         DATA_STRING, "Hollarm-Remote",
                "id",       "",         DATA_FORMAT, "%06x", DATA_INT, id,
                "button",   "Button",   DATA_FORMAT, "%x", DATA_INT, button,
                "mic",      "Integrity", DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return ret;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        "mic",
        NULL,
};

r_device const hollarm = {
        .name        = "Hollarm alarm system remotes",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 1600,
        .gap_limit   = 2000,
        .reset_limit = 5000,
        .tolerance   = 250,
        .decode_fn   = &hollarm_decode,
        .disabled    = 1, // weak integrity check
        .fields      = output_fields,
};
