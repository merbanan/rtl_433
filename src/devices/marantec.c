/** @file
    Marantec garage door remote and Marantec24 clone remote.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Marantec garage door remote.

Fixed code remote, seen on 868.35 MHz and 433.92 MHz.
Tested at 868.35 MHz with -s 1024k, nothing was received with -s 250k.

Manchester coded with a half-bit time of 1000 us (500 bit/s), a 1 bit is low then high.
Each message is 49 bits, sent MSB first, and repeated after a 10 ms gap.
The low half of the first bit merges with the gap, so a message starts on the mid-bit
rising edge of the first bit, which is always 1. A trailing low half also merges with
the gap, a message ending in 00 is then sliced as 48 bits.

Data layout:

    1 KKKKKKKK SSSSSSSSSSSSSSSSSSSS BBBB XXXXXXXX CCCCCCCC

- 1: 1 bit, always 1
- K: 8 bit constant, 0x30 on all known remotes
- S: 20 bit serial
- B: 4 bit button
- X: 8 bit serial (low part), 0x86 on all known remotes
- C: 8 bit CRC-8, poly 0x1d, init 0x01, over the 41 bits before it as 6 bytes, right aligned
  (the first byte is the leading 1 bit). This is the same as init 0x00 over the 5 bytes after it.

Format string:

    START:b CONST:8h SERIAL:20h BUTTON:4h SERIAL:8h CRC:8h

Flex decoder:

    rtl_433 -X 'n=marantec,m=OOK_MC_ZEROBIT,s=1000,l=0,r=4000,t=300,invert'

The id is the 20 bit serial followed by the 8 bit low serial, 7 hex digits.
*/

static int marantec_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Rising edge is 0 with the zerobit slicer, a 1 bit here is low then high
    bitbuffer_invert(bitbuffer);

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        int bits = bitbuffer->bits_per_row[row];
        // The last bit is lost if the message ends in 00, it is 0 then.
        // A repeat can have one extra leading bit from the previous gap.
        if (bits < 48 || bits > 50) {
            continue;
        }
        int start = bits == 50 ? 2 : 1;
        if (!bitrow_get_bit(bitbuffer->bb[row], start - 1)) {
            continue; // First bit is always 1
        }

        uint8_t b[6];
        bitbuffer_extract_bytes(bitbuffer, row, start, b, 48);

        if (crc8(b, 5, 0x1d, 0x00) != b[5]) {
            decoder_log(decoder, 2, __func__, "CRC error");
            continue;
        }
        // Reject all zero data, it passes the CRC
        if (!b[0] && !b[1] && !b[2] && !b[3] && !b[4]) {
            continue;
        }

        int serial = (b[1] << 20) | (b[2] << 12) | ((b[3] & 0xf0) << 4) | b[4];
        int button = b[3] & 0x0f;

        char id_str[8];
        snprintf(id_str, sizeof(id_str), "%07X", serial);

        /* clang-format off */
        data_t *data = data_make(
                "model",    "",             DATA_STRING, "Marantec-Remote",
                "id",       "",             DATA_STRING, id_str,
                "button",   "Button",       DATA_INT,    button,
                "mic",      "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return DECODE_ABORT_LENGTH;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        "mic",
        NULL,
};

r_device const marantec = {
        .name        = "Marantec garage door remote",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 1000,
        .long_width  = 0,    // not used
        .reset_limit = 4000, // below the 10 ms message gap
        .tolerance   = 300,
        .decode_fn   = &marantec_decode,
        .fields      = output_fields,
};

/**
Marantec24 clone remote.

Not a Marantec protocol: a fixed code remote sold as a Marantec replacement (a cloner),
named Marantec24 by the Flipper Zero firmware.

PWM coded, each bit is 4000 us: a 1 bit is an 800 us pulse and a 3200 us gap, a 0 bit
is a 1600 us pulse and a 2400 us gap. Each message is 24 bits, sent MSB first, and
repeated after a 15200 us gap.

Data layout:

    SSSSSSSSSSSSSSSSSSSS BBBB

- S: 20 bit serial
- B: 4 bit button

There is no integrity check, at least two identical repeats are required.

Format string:

    SERIAL:20h BUTTON:4h

Flex decoder:

    rtl_433 -X 'n=marantec24,m=OOK_PWM,s=800,l=1600,g=5000,r=20000,t=200,bits=24,repeats>=2'
*/

static int marantec24_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 24);
    if (row < 0 || bitbuffer->bits_per_row[row] != 24) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[row];

    // Reject all zero and all one codes
    if ((!b[0] && !b[1] && !b[2]) || (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff)) {
        return DECODE_FAIL_SANITY;
    }

    int serial = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);
    int button = b[2] & 0x0f;

    char id_str[6];
    snprintf(id_str, sizeof(id_str), "%05X", serial);

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",             DATA_STRING, "Marantec24-Clone",
            "id",       "",             DATA_STRING, id_str,
            "button",   "Button",       DATA_INT,    button,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const marantec24_output_fields[] = {
        "model",
        "id",
        "button",
        NULL,
};

r_device const marantec24 = {
        .name        = "Marantec24 clone remote",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 800,
        .long_width  = 1600,
        .gap_limit   = 5000,  // above the 3200 us bit gap
        .reset_limit = 20000, // above the 15200 us message gap
        .tolerance   = 200,
        .decode_fn   = &marantec24_decode,
        .disabled    = 1, // no integrity check
        .fields      = marantec24_output_fields,
};
