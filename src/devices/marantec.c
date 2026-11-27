/** @file
    Marantec garage door remotes.

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

The Marantec24 clone remotes (PWM, 24 bit, no integrity check) are decoded by conf/Marantec24.conf.
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
