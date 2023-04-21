/** @file
    Decoder for Linear Megacode Garage & Gate Remotes.

    Copyright (C) 2021 Aaron Spangler <aaron777@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Decoder for Linear Megacode Garage & Gate Remotes. (Fixed/non-rolling code).

A Linear Megacode transmission consists of 24 bit frames starting with the
most significant bit and ending with the least. Each of the 24 bit frames is
6 milliseconds wide and always contains a single 1 millisecond pulse. A frame
with more than 1 pulse or a frame with no pulse is invalid and a receiver
should reset and begin watching for another start bit.

The position of the pulse within the bit frame determines if it represents a
binary 0 or binary 1. If the pulse is within the first half of the frame, it
represents binary 0. The second half of the frame represents a binary 1.

References:
- https://github.com/aaronsp777/megadecoder/blob/main/Protocol.md
- https://wiki.cuvoodoo.info/doku.php?id=megacode
- https://fccid.io/EF4ACP00872/Test-Report/Megacode-2-112615.pdf

Example:

    raw: 8DF78A
    facility: 1 id: 48881 button: 2
    bits: 10010000010000010000000010000010010000000010000010000010000010000010010000000010000010000010000010010000010000010000000010010000000010010... (up to 148 bits)

    $ rtl_433 -g 100 -f 318M -X "n=Megacode,m=OOK_PCM,s=1000,l=1000,g=8000,r=10000"

*/

#include "decoder.h"

static int megacode_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 1, 144);
    if (row < 0)
        return DECODE_ABORT_LENGTH;
    int l = bitbuffer->bits_per_row[row];
    if (l < 136 || l > 148)
        return DECODE_ABORT_LENGTH;

    uint32_t raw      = 0;
    int frame_counter = 0;
    uint8_t *b        = bitbuffer->bb[row];

    for (int i = 0; i < l; i++) {
        if ((b[i / 8] << (i % 8)) & 0x80) {
            if ((i + 4) % 6 > 2)
                raw |= 0x800000 >> ((i + 4) / 6);
            frame_counter++;
        }
    }

    if (frame_counter != 24)
        return DECODE_FAIL_SANITY;

    int facility = (raw >> 19) & 0xf;
    int id       = (raw >> 3) & 0xffff;
    int button   = raw & 0x7;

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",               DATA_STRING, "Megacode-Remote",
            "id",       "Transmitter ID", DATA_INT,    id,
            "raw",      "Raw",            DATA_FORMAT, "%06X", DATA_INT, raw,
            "facility", "Facility Code",  DATA_INT,    facility,
            "button",   "Button",         DATA_INT,    button,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "raw",
        "facility",
        "button",
        NULL,
};

r_device const megacode = {
        .name        = "Linear Megacode Garage/Gate Remotes",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 1000,
        .long_width  = 1000,
        .gap_limit   = 9000,
        .reset_limit = 20000,
        .decode_fn   = &megacode_callback,
        .fields      = output_fields,
};
