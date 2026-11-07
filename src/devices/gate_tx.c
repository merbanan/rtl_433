/** @file
    Gate TX gate remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Gate TX gate remotes.

Seen on 433.92 MHz. A 17 ms gap, a 700 us start pulse, then 24 bits. Each bit is a gap and a
pulse, a 1 bit is a 700 us gap and a 350 us pulse, a 0 bit is a 350 us gap and a 700 us pulse.
Sliced as PWM the start pulse is a leading 0 bit, a row has 25 bits.

Data layout, after reversing the 24 bits (LSB first):

    SSSSSSSS SSSSSSSS BBBB SSSS

- S: 20 bit serial, the bytes 0 and 1 and the low nibble of byte 2, as serial bits 19..4 and 3..0
- B: 4 bit button

There is no integrity check. The messages are 17 ms apart, each is a package of its own, so
only the exact length and the start bit are checked.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/gate_tx.c`.

Flex decoder (raw code only):

    rtl_433 -X 'n=gate_tx,m=OOK_PWM,s=350,l=700,g=1200,r=10000,t=150,bits=25'
*/

static int gate_tx_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Each message is a package of its own, the 17 ms gap ends it
    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] != 25) {
        return DECODE_ABORT_LENGTH;
    }
    int row = 0;

    // Start bit
    if (bitbuffer->bb[row][0] & 0x80) {
        return DECODE_FAIL_SANITY;
    }

    uint8_t b[3];
    bitbuffer_extract_bytes(bitbuffer, row, 1, b, 24);
    int code = (b[0] << 16) | (b[1] << 8) | b[2];

    // Reject all 0 and all 1 codes
    if (code == 0x000000 || code == 0xffffff) {
        return DECODE_FAIL_SANITY;
    }

    // Reverse the 24 bits
    int rev    = (reverse8(b[2]) << 16) | (reverse8(b[1]) << 8) | reverse8(b[0]);
    int id     = ((rev & 0xff) << 12) | (((rev >> 8) & 0xff) << 4) | ((rev >> 20) & 0x0f);
    int button = (rev >> 16) & 0x0f;

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",         DATA_STRING, "GateTX",
            "id",       "",         DATA_FORMAT, "%05x", DATA_INT, id,
            "button",   "Button",   DATA_INT,    button,
            "code",     "Code",     DATA_FORMAT, "%06x", DATA_INT, code,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        "code",
        NULL,
};

r_device const gate_tx = {
        .name        = "Gate TX gate remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 350,
        .long_width  = 700,
        .gap_limit   = 1200,
        .reset_limit = 10000,
        .tolerance   = 150,
        .decode_fn   = &gate_tx_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields,
};
