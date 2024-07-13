/** @file
    Generic doorbell implementation for Elro DB286A devices.

    Copyright (C) 2016 Fabian Zaremba <fabian@youremail.eu>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Generic doorbell implementation for Elro DB286A devices.

Note that each device seems to have two codes, which alternate
for every other button press.

short is 456 us pulse, 1540 us gap
long is 1448 us pulse, 544 us gap
packet gap is 7016 us

Example code: 37f62a6c80
*/

#include "decoder.h"

static int elro_db286a_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // 33 bits expected, 5 minimum packet repetitions (14 expected)
    int row = bitbuffer_find_repeated_row(bitbuffer, 5, 33);

    if (row < 0 || bitbuffer->bits_per_row[row] != 33)
        return DECODE_ABORT_LENGTH;

    uint8_t *b = bitbuffer->bb[row];

    // 32 bits, trailing bit is dropped
    char id_str[4 * 2 + 1];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x%02x", b[0], b[1], b[2], b[3]);

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",        DATA_STRING, "Elro-DB286A",
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

r_device const elro_db286a = {
        .name        = "Elro DB286A Doorbell",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 456,
        .long_width  = 1448,
        .gap_limit   = 2000,
        .reset_limit = 8000,
        .decode_fn   = &elro_db286a_callback,
        .disabled    = 1,
        .fields      = output_fields,
};
