/** @file
    Feron LED light remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Feron LED light remotes.

Seen on 433.92 MHz. PWM coded, a 1 bit is a 750 us pulse and a 350 us gap, a 0 bit is a
350 us pulse and a 750 us gap. A message is 32 bits, then a 500 us gap, a 500 us pulse and a
4.5 ms gap, and is repeated.

Data layout:

    IIII CC XX

- I: 16 bit id
- C: 8 bit command, e.g. 0x85 on, 0x84 off, 0x86 brighter, 0x87 darker, 0x82 mode
- X: command inverted

Two identical messages are required.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/feron.c`.

Flex decoder:

    rtl_433 -X 'n=feron,m=OOK_PWM,s=350,l=750,g=2000,r=10000,t=150,invert,bits>=32'
*/

static int feron_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    // The 500 us end pulse can be sliced as a 33rd bit, compare the first 32 bits
    int row = -1;
    for (int i = 0; i < bitbuffer->num_rows && row < 0; ++i) {
        if (bitbuffer->bits_per_row[i] != 32 && bitbuffer->bits_per_row[i] != 33) {
            continue;
        }
        for (int j = i + 1; j < bitbuffer->num_rows; ++j) {
            uint8_t *ri = bitbuffer->bb[i];
            uint8_t *rj = bitbuffer->bb[j];
            if ((bitbuffer->bits_per_row[j] == 32 || bitbuffer->bits_per_row[j] == 33) && ri[0] == rj[0] && ri[1] == rj[1] && ri[2] == rj[2] && ri[3] == rj[3]) {
                row = i;
                break;
            }
        }
    }
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *b = bitbuffer->bb[row];

    // Command and inverted command
    if ((b[2] ^ b[3]) != 0xff) {
        return DECODE_FAIL_MIC;
    }

    int id      = (b[0] << 8) | b[1];
    int command = b[2];

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",         DATA_STRING, "Feron-Remote",
            "id",       "",         DATA_FORMAT, "%04x", DATA_INT, id,
            "command",  "Command",  DATA_FORMAT, "%02x", DATA_INT, command,
            "mic",      "Integrity", DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "command",
        "mic",
        NULL,
};

r_device const feron = {
        .name        = "Feron LED light remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 350,
        .long_width  = 750,
        .gap_limit   = 2000,
        .reset_limit = 10000,
        .tolerance   = 150,
        .decode_fn   = &feron_decode,
        .disabled    = 1, // weak integrity check
        .fields      = output_fields,
};
