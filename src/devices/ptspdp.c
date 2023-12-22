/** @file
    Generic doorbell implementation for PTSPDP devices.

    Copyright (C) 2016 Fabian Zaremba <fabian@youremail.eu>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Generic doorbell implementation for PTSPDP devices.

Based on flex decoder 
-X m=OOK_PPM,s=208,l=596,r=6068,g=1000,t=0,y=908,match={24}db929c,repeats>=10

*/

#include "decoder.h"

static int ptspdp_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    //printf("PTSPDP: called!\n");

    //bitbuffer_debug(bitbuffer);

    // 24 bits expected, 10 minimum packet repetitions
    int row = bitbuffer_find_repeated_row(bitbuffer, 10, 24);

    if (row < 0 || bitbuffer->bits_per_row[row] < 24) {
        //printf("PTSPDP: abort, too short row: %d bits_per_row: %d\n", row, bitbuffer->bits_per_row[row]);
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[row];

    // 24 bits, trailing bit is dropped
    char id_str[3 * 2 + 1];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x", b[0], b[1], b[2]);

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",        DATA_STRING, "PTSPDP",
            "id",       "ID",      DATA_STRING, id_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        NULL,
};

r_device const ptspdp = {
        .name        = "PTSPDP Doorbell",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 208,
        .long_width  = 596,
        .gap_limit   = 1000,
        .reset_limit = 6068,
        .sync_width  = 908,
        .tolerance   = 0,
        .decode_fn   = &ptspdp_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
