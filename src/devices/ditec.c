/** @file
    Ditec GOL4 gate remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Ditec (Entrematic) GOL4 rolling code gate remotes.

Seen on 433.92 MHz. A message is a 24 ms gap, an 800 us start pulse, then 54 bits. Each bit
is a gap followed by a pulse, a 1 bit is a 400 us gap and a 1100 us pulse, a 0 bit is a
1100 us gap and a 400 us pulse. The message is repeated about 4 times.

The 54 bits (7 bytes, the last 2 bits unused) are scrambled without a key:

- bytes 2 to 6 are shifted left by 3 bits as one 40 bit value
- bytes 0, 1, 3, 4, 5, 6 are bit reversed
- byte 2 is bit reversed, inverted and its nibbles swapped (bit 5 is cleared)
- byte 5 and 6 are a 16 bit counter, xor 0xa7 and 0x69, then stepped with x = 21 * x + 1:
  byte 6 by (byte 5) steps, byte 5 by 0xfe steps, byte 5 bit 0 is flipped if byte 6 bit 7 is set
- after the shift, byte 6 bit 2 is the check bit: set if the parity of byte 5 and 6
  (before the shift) differs

Unscrambled data layout:

    S0 S2 BB S3 S1 CL CH

- S: 32 bit serial, reported as S0 S1 S2 S3
- B: 0x7 and the button (0x1, 0x2, 0x4 or 0x8), or 0xf0 in programming mode
- C: 16 bit counter, low byte first

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/ditec_gol4.c`.

Flex decoder:

    rtl_433 -X 'n=ditec_gol4,m=OOK_PWM,s=400,l=1100,g=2000,r=12000,t=200,invert,bits=54'
*/

static uint8_t ditec_gol4_lcg(uint8_t x, int steps)
{
    for (int i = 0; i < steps; ++i) {
        x = (uint8_t)(21 * x + 1);
    }
    return x;
}

static int ditec_gol4_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    int row = -1;
    for (int i = 0; i < bitbuffer->num_rows; ++i) {
        if (bitbuffer->bits_per_row[i] == 54 && bitbuffer->syncs_before_row[i] > 0) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[7];
    bitbuffer_extract_bytes(bitbuffer, row, 0, b, 54);

    int check = (b[6] >> 2) & 1;

    // Undo the 3 bit shift of bytes 2 to 6
    int carry = 0;
    for (int r = 0; r < 3; ++r) {
        for (int i = 2; i < 7; ++i) {
            int next = b[i] & 1;
            b[i]     = (uint8_t)((b[i] >> 1) | (carry << 7));
            carry    = next;
        }
    }

    // Validate check bit
    if (check != (parity8(b[5]) ^ parity8(b[6]))) {
        return DECODE_FAIL_MIC;
    }

    uint8_t b2 = b[2] & 0xdf;
    b2         = (uint8_t)((b2 << 4) | (b2 >> 4));
    b[2]       = reverse8(~b2);
    b[0]       = reverse8(b[0]);
    b[1]       = reverse8(b[1]);
    b[3]       = reverse8(b[3]);
    b[4]       = reverse8(b[4]);
    b[5]       = reverse8(b[5]);
    b[6]       = reverse8(b[6]);

    // Counter
    if (b[6] & 0x80) {
        b[5] ^= 1;
    }
    b[5] = ditec_gol4_lcg(b[5], 256 - 0xfe);
    b[6] = ditec_gol4_lcg(b[6], (256 - b[5]) & 0xff);
    b[5] ^= 0xa7;
    b[6] ^= 0x69;

    // Button byte
    int learn  = b[2] == 0xf0;
    int button = b[2] & 0x0f;
    if (!learn && ((b[2] >> 4) != 0x7 || (button != 1 && button != 2 && button != 4 && button != 8))) {
        return DECODE_FAIL_SANITY;
    }

    char serial_str[9];
    snprintf(serial_str, sizeof(serial_str), "%02X%02X%02X%02X", b[0], b[4], b[1], b[3]);
    int counter = (b[6] << 8) | b[5];

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "Ditec-GOL4",
            "id",           "",             DATA_STRING, serial_str,
            "button",       "Button",       DATA_INT,    button,
            "learn",        "Learn mode",   DATA_INT,    learn,
            "counter",      "Counter",      DATA_INT,    counter,
            "mic",          "Integrity",    DATA_STRING, "PARITY",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        "learn",
        "counter",
        "mic",
        NULL,
};

r_device const ditec_gol4 = {
        .name        = "Ditec GOL4 gate remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 400,
        .long_width  = 1100,
        .sync_width  = 800,   // start pulse
        .gap_limit   = 2000,  // above the 1100 us bit gap
        .reset_limit = 12000, // below the 24 ms message gap
        .tolerance   = 200,
        .decode_fn   = &ditec_gol4_decode,
        .disabled    = 1, // weak integrity check
        .fields      = output_fields,
};
