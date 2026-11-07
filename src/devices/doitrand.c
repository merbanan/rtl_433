/** @file
    Doitrand gate remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Doitrand gate remotes with 10 DIP switches.

Seen on 433.92 MHz. A 24.8 ms gap, a 700 us start pulse, then 37 bits. Each bit is a gap and a
pulse, a 1 bit is a 1100 us gap and a 400 us pulse, a 0 bit is a 400 us gap and a 1100 us pulse.
The start pulse is sliced as a sync, a row has 37 bits.

Data layout (bit 36 sent first), DIP switches as shown by the Flipper Zero:

- DIP 1: bit 24 or bit 15
- DIP 2 to 5: bits 32 to 29
- DIP 6 to 9: bits 36 to 33
- DIP 10: bit 25
- button: bits 19, 18
- the other bits are not known

There is no integrity check, the start pulse and the exact length are checked.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/doitrand.c`.

Flex decoder (raw code only):

    rtl_433 -X 'n=doitrand,m=OOK_PWM,s=400,l=1100,y=750,g=2000,r=10000,bits=37'
*/

static int doitrand_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // A 37 bit row after the start pulse
    int row = -1;
    for (int i = 0; i < bitbuffer->num_rows; ++i) {
        if (bitbuffer->bits_per_row[i] == 37 && bitbuffer->syncs_before_row[i] > 0) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[5] = {0};
    bitbuffer_extract_bytes(bitbuffer, row, 0, b, 37);
    uint64_t data = ((uint64_t)b[0] << 29) | ((uint64_t)b[1] << 21) | (b[2] << 13) | (b[3] << 5) | (b[4] >> 3);

    // Reject all 0 and all 1 codes
    if (data == 0 || data == 0x1fffffffffULL) {
        return DECODE_FAIL_SANITY;
    }

    int dip_bits = (int)((data >> 24) | ((data >> 15) & 1));
    int button   = (data >> 18) & 0x3;

    int const order[10] = {0, 8, 7, 6, 5, 12, 11, 10, 9, 1};
    char dip[11];
    for (int i = 0; i < 10; ++i) {
        dip[i] = (dip_bits >> order[i]) & 1 ? '1' : '0';
    }
    dip[10] = '\0';

    char code[11];
    snprintf(code, sizeof(code), "%02x%08x", (unsigned)(data >> 32), (unsigned)(data & 0xffffffff));

    /* clang-format off */
    data_t *output = data_make(
            "model",    "",             DATA_STRING, "Doitrand",
            "dip",      "Dip switches", DATA_STRING, dip,
            "button",   "Button",       DATA_INT,    button,
            "code",     "Code",         DATA_STRING, code,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, output);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "dip",
        "button",
        "code",
        NULL,
};

r_device const doitrand = {
        .name        = "Doitrand gate remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 400,
        .long_width  = 1100,
        .sync_width  = 750, // start pulse
        .gap_limit   = 2000,
        .reset_limit = 10000,
        .decode_fn   = &doitrand_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields,
};
