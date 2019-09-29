/* Generic remote Blyss DC5-UK-WH as sold by B&Q
 *
 * DC5-UK-WH pair with receivers, the codes used may be specific to a receiver - use with caution
 *
 * warmup pulse 5552 us, 2072 gap
 * short is 512 us pulse, 1484 us gap
 * long is 1508 us pulse, 488 us gap
 * packet gap is 6964 us
 *
 * Copyright (C) 2016 John Jore
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

static int blyss_callback(r_device *decoder,bitbuffer_t *bitbuffer) {
    data_t *data;
    uint8_t *b;
    char id_str[16];

    for (int i = 0; i < bitbuffer->num_rows; ++i) {
        if (bitbuffer->bits_per_row[i] != 33) // last row is 32
            continue;

        b = bitbuffer->bb[i];

        //This needs additional validation, but works on mine. Suspect each DC5-UK-WH uses different codes as the transmitter
        //is paired to the receivers to avoid being triggered by the neighbours transmitter ?!?
        if (((b[0] != 0xce) || (b[1] != 0x8e) || (b[2] != 0x2a) || (b[3] != 0x6c) || (b[4] != 0x80)) &&
                ((b[0] != 0xe7) || (b[1] != 0x37) || (b[2] != 0x7a) || (b[3] != 0x2c) || (b[4] != 0x80)))
            continue;

        sprintf(id_str, "%02x%02x%02x%02x", b[0], b[1], b[2], b[3]);

        data = data_make(
                "model",    "", DATA_STRING, "Blyss-DC5ukwh",
                "id",       "", DATA_STRING, id_str,
                NULL);
        decoder_output_data(decoder, data);

        return 1;
    }

    return 0;
}

static char *output_fields[] = {
    "model",
    "id",
    NULL
};

r_device blyss = {
    .name           = "Blyss DC5-UK-WH",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 500,
    .long_width     = 1500,
    .gap_limit      = 2500,
    .reset_limit    = 8000,
    .decode_fn      = &blyss_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
