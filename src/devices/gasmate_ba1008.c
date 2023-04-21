/** @file
    Gasmate BA1008 meat thermometer.

    Copyright (C) 2023 Christian W. Zuckschwerdt <zany@triq.net>
    based on protocol analysis by Lucy Winters

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Gasmate BA1008 meat thermometer.

Notably this protocol does not feature ID or CHANNEL information.

S.a. #2324

Data Layout:

    PF TT ?? ?A

- P: (4 bit) preamble/model/type? fixed 0xf
- F: (4 bit) Unknown bit; Sign bit; 2-bit temperature 100ths (BCD)
- T: (8 bit) temperature 10ths and 1ths (BCD)
- ?: (12 bit) unknown value
- A: (4 bit) checksum, nibble-wide add with carry

Raw data:

    F4040BFB [-04C]
    F4060BF9 [-06C]
    F4100BEF [-10C]
    f0030ffc [+03C]
    F0230FDC [+23C]
    F0310FCE [+31C]

Format string:

    PREAMBLE?h ?b SIGN:b TEMP:2hhhC ?hhh CHK:h

*/

static int gasmate_ba1008_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1) {
        decoder_log(decoder, 2, __func__, "Row check fail");
        return DECODE_ABORT_LENGTH;
    }

    int row = 0;
    uint8_t *b = bitbuffer->bb[row];
    // we expect 32 bits
    if (bitbuffer->bits_per_row[row] != 32) {
        decoder_log(decoder, 2, __func__, "Length check fail");
        return DECODE_ABORT_LENGTH;
    }

    // preamble/model/type and first flag bit check
    if ((b[0] & 0xf8) != 0xf0) {
        decoder_log(decoder, 2, __func__, "Model check fail");
        return DECODE_ABORT_EARLY;
    }

    // check checksum
    if ((add_nibbles(b, 4) & 0x0f) != 0x0c) {
        decoder_log(decoder, 2, __func__, "Checksum fail");
        return DECODE_FAIL_MIC;
    }

    int sign     = (b[0] & 0x04) >> 2;
    int huns     = (b[0] & 0x03);
    int tens     = (b[1] & 0xf0) >> 4;
    int ones     = (b[1] & 0x0f);
    int temp_raw = huns * 100 + tens * 10 + ones;
    int temp_c   = sign ? -temp_raw : temp_raw;
    int unknown1 = (b[2] << 4) | (b[3] >> 4);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Gasmate-BA1008",
            "temperature_C",    "Temperature_C",    DATA_FORMAT, "%d C", DATA_INT, temp_c,
            "unknown_1",        "Unknown Value",    DATA_FORMAT, "%03x", DATA_INT,    unknown1,
            "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "temperature_C",
        "unknown_1",
        "mic",
        NULL,
};

r_device const gasmate_ba1008 = {
        .name        = "Gasmate BA1008 meat thermometer",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 536,
        .long_width  = 1668,
        .reset_limit = 2000,
        .decode_fn   = &gasmate_ba1008_decode,
        .fields      = output_fields,
};
