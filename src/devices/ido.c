/** @file
    iDo 117 and 111 gate remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
iDo 117 and 111 rolling code gate remotes.

Seen on 433.92 MHz. PPM coded, 450 us pulses, a 1 bit is a 450 us gap, a 0 bit is a 1450 us
gap. A message is a 4.5 ms pulse and a 4.5 ms gap, then 48 bits. A button press sends about
20 messages back to back, each with a new hop code.

Data layout, sent LSB first, fix part then hop part:

    SSSSSSSSSSSSSSSSSSSS BBBB HHHHHHHHHHHHHHHHHHHHHHHH

- S: 20 bit serial
- B: 4 bit button
- H: 24 bit hop code, changes with every message

There is no integrity check, so two messages with the same fix part are required. The hop code
of the first message is reported.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/ido.c`.

Flex decoder:

    rtl_433 -X 'n=ido,m=OOK_PPM,s=450,l=1450,g=3000,r=10000,t=300,invert,bits=48'
*/

static int ido_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    // First row of 48 bits with a second row of the same fix part
    int row = -1;
    for (int i = 0; i < bitbuffer->num_rows && row < 0; ++i) {
        if (bitbuffer->bits_per_row[i] != 48) {
            continue;
        }
        for (int j = i + 1; j < bitbuffer->num_rows; ++j) {
            uint8_t *ri = bitbuffer->bb[i];
            uint8_t *rj = bitbuffer->bb[j];
            if (bitbuffer->bits_per_row[j] == 48 && ri[0] == rj[0] && ri[1] == rj[1] && ri[2] == rj[2]) {
                row = i;
                break;
            }
        }
    }
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *b   = bitbuffer->bb[row];
    uint32_t fix = ((uint32_t)reverse8(b[2]) << 16) | (reverse8(b[1]) << 8) | reverse8(b[0]);
    uint32_t hop = ((uint32_t)reverse8(b[5]) << 16) | (reverse8(b[4]) << 8) | reverse8(b[3]);

    // Reject all zero and all one fix parts
    if (fix == 0 || fix == 0xffffff) {
        return DECODE_FAIL_SANITY;
    }

    int serial = fix & 0xfffff;
    int button = fix >> 20;

    char serial_str[6];
    snprintf(serial_str, sizeof(serial_str), "%05X", serial);
    char hop_str[7];
    snprintf(hop_str, sizeof(hop_str), "%06X", hop);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",         DATA_STRING, "iDo-Remote",
            "id",           "",         DATA_STRING, serial_str,
            "button",       "Button",   DATA_INT,    button,
            "encrypted",    "",         DATA_STRING, hop_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        "encrypted",
        NULL,
};

r_device const ido = {
        .name        = "iDo 117/111 gate remotes",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 450,
        .long_width  = 1450,
        .gap_limit   = 3000,  // below the 4.5 ms preamble gap
        .reset_limit = 10000, // above the 4.5 ms preamble gap
        .tolerance   = 300,
        .decode_fn   = &ido_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields,
};
