/** @file
    Jarolift shutter remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Jarolift shutter remotes.

Seen on 433.92 MHz. KeeLoq based, with the timing of the Microchip HCS200 (PWM, a 1 bit is a
400 us pulse and an 800 us gap, a 0 bit is an 800 us pulse and a 400 us gap) but its own frame:
a 1500 us pulse and 8 short pulses as warm-up, a 3.8 ms gap, then 72 bits, LSB first.

| 0-31  | Hopping code, KeeLoq encrypted with the Jarolift key
| 32-59 | Serial number
| 60-63 | Button
| 64-71 | Group

Button as the Flipper Zero shows it: 0x1 learn, 0x2 down, 0x4 stop, 0x8 up.

The hopping code is not decrypted. There is no integrity check, so the decoder is disabled by
default.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/jarolift.c`.

    rtl_433 -R 0 -X 'n=jarolift,m=OOK_PWM,s=370,l=772,r=9000,g=1500,t=152'
*/

#include "decoder.h"

static int jarolift_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // 8 bit warm-up row (short pulses), then the 72 bit data row
    int row = -1;
    for (int r = 1; r < bitbuffer->num_rows; ++r) {
        if (bitbuffer->bits_per_row[r] == 72 && bitbuffer->bits_per_row[r - 1] == 8 && bitbuffer->bb[r - 1][0] == 0xff) {
            row = r;
            break;
        }
    }
    if (row < 0) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[row];

    // Reject all one data (only short pulses)
    if (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff && b[3] == 0xff && b[4] == 0xff && b[5] == 0xff && b[6] == 0xff && b[7] == 0xff) {
        return DECODE_FAIL_SANITY;
    }

    // LSB first
    uint32_t encrypted = ((unsigned)reverse8(b[3]) << 24) | (reverse8(b[2]) << 16) | (reverse8(b[1]) << 8) | (reverse8(b[0]));
    int serial         = (reverse8(b[7] & 0xf0) << 24) | (reverse8(b[6]) << 16) | (reverse8(b[5]) << 8) | (reverse8(b[4]));
    int button         = reverse8(b[7] & 0x0f) >> 4;
    int group          = reverse8(b[8]);

    char encrypted_str[9];
    snprintf(encrypted_str, sizeof(encrypted_str), "%08X", encrypted);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "Jarolift-Remote",
            "id",           "",             DATA_FORMAT, "%07x", DATA_INT, serial,
            "button",       "Button",       DATA_INT,    button,
            "group",        "Group",        DATA_FORMAT, "%02x", DATA_INT, group,
            "encrypted",    "",             DATA_STRING, encrypted_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        "group",
        "encrypted",
        NULL,
};

r_device const jarolift = {
        .name        = "Jarolift shutter remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 370,
        .long_width  = 772,
        .gap_limit   = 1500,
        .reset_limit = 9000,
        .tolerance   = 152,
        .decode_fn   = &jarolift_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields,
};
