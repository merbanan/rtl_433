/** @file
    Intertechno remotes.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Intertechno remotes.

Intertechno remote labeled ITT-1500 that came with 3x ITR-1500 remote outlets. The set is labeled IT-1500.
The PPM consists of a 220µs high followed by 340µs or 1400µs of gap.

There is another type of remotes that have an ID prefix of 0x56 and slightly shorter timing.

 */

#include "decoder.h"

static int intertechno_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitrow_t *bb = bitbuffer->bb;
    uint8_t *b = bitbuffer->bb[1];

    if (bb[0][0] != 0 || (bb[1][0] != 0x56 && bb[1][0] != 0x69))
        return DECODE_ABORT_EARLY;

    char id_str[11];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4]);
    int slave   = b[7] & 0x0f;
    int master  = (b[7] & 0xf0) >> 4;
    int command = b[6] & 0x07;

    /* clang-format off */
    data_t *data = NULL;
    data = data_str(data, "model",            "",     NULL,         "Intertechno-Remote");
    data = data_str(data, "id",               "",     NULL,         id_str);
    data = data_int(data, "slave",            "",     NULL,         slave);
    data = data_int(data, "master",           "",     NULL,         master);
    data = data_int(data, "command",          "",     NULL,         command);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "slave",
        "master",
        "command",
        NULL,
};

r_device const intertechno = {
        .name        = "Intertechno 433",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 330,
        .long_width  = 1400,
        .gap_limit   = 1700,
        .reset_limit = 10000,
        .decode_fn   = &intertechno_callback,
        .disabled    = 1,
        .fields      = output_fields,
};
