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
    data_t *data;
    bitrow_t *bb = bitbuffer->bb;
    uint8_t *b = bitbuffer->bb[1];
    char id_str[11];
    int slave;
    int master;
    int command;

    if (bb[0][0] != 0 || (bb[1][0] != 0x56 && bb[1][0] != 0x69))
        return DECODE_ABORT_EARLY;

    sprintf(id_str, "%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4]);
    slave   = b[7] & 0x0f;
    master  = (b[7] & 0xf0) >> 4;
    command = b[6] & 0x07;

    /* clang-format off */
    data = data_make(
            "model",            "",     DATA_STRING,    "Intertechno-Remote",
            "id",               "",     DATA_STRING,    id_str,
            "slave",            "",     DATA_INT,       slave,
            "master",           "",     DATA_INT,       master,
            "command",          "",     DATA_INT,       command,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "type",
        "id",
        "slave",
        "master",
        "command",
        NULL,
};

r_device intertechno = {
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
