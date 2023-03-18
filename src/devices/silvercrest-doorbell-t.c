/** @file
    Silvercrest Doorbell T decoder.

    Copyright (C) 2018 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int silvercrest_doorbell_t_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Silvercrest Doorbell T decoder.

Model number: STKK 16 B1
Manufactured: 2022-09
IAN: 498825_2204

Data layout:

    - byte 0: probably some ID

*/

static int silvercrest_doorbell_t_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{

    data_t *data;

    if (bitbuffer->num_rows == 1 && bitbuffer->bits_per_row[0] == 8 && bitbuffer->bb[0][0] == 0xf9) {

        /* clang-format off */
        data = data_make(
                "model", "", DATA_STRING, "Silvercrest Doorbell T(STKK 16 B1)",
                "id", "", DATA_INT, bitbuffer->bb[0][0],
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return DECODE_ABORT_EARLY;
}

static char const *const output_fields[] = {
        "model",
        "id",
        NULL,
};

r_device const silvercrest_doorbell_t = {
        .name        = "Silvercrest Doorbell T(STKK 16 B1)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 25,
        .long_width  = 75,
        .reset_limit = 12000,
        .gap_limit   = 5000,
        .decode_fn   = &silvercrest_doorbell_t_decode,
        .fields      = output_fields,
};
