/* Silverline doorbell.
 *
 * Copyright (C) 2018 Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "decoder.h"


static int silverline_doorbell_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    uint8_t *b; // bits of a row
    uint8_t channel;
    uint8_t sound;
    data_t *data;

    b = bitbuffer->bb[1];
    if (bitbuffer->bits_per_row[1] != 25) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "Wrong payload length, %01x\n", bitbuffer->bits_per_row[1]);
        }
        return 0;
    }

    channel = (b[0] >> 6) & 0x01;
    channel = channel << 1;
    channel |= (b[0] >> 4) & 0x01;
    channel = channel << 1;
    channel |= (b[0] >> 2) & 0x01;
    channel = channel << 1;
    channel |= (b[0] >> 0) & 0x01;
    channel = channel << 1;
    channel |= (b[1] >> 6) & 0x01;
    channel = channel << 1;
    channel |= (b[1] >> 4) & 0x01;

    sound = (b[2] >> 6) & 0x01;
    sound = sound << 1;
    sound |= (b[2] >> 4) & 0x01;
    sound = sound << 1;
    sound |= (b[2] >> 2) & 0x01;

    data = data_make(
            "model", "", DATA_STRING, "Silverline Doorbell",
            "channel", "", DATA_INT, channel,
            "sound", "", DATA_INT, sound,
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
        "model",
        "channel",
        "sound",
        NULL
};

r_device silverline_doorbell = {
        .name           = "Silverline Doorbell",
        .modulation     = OOK_PULSE_PWM,
        .short_width    = 120,
        .long_width     = 404,
        .reset_limit    = 4472,
        .gap_limit      = 468,
        .tolerance      = 112,
        .decode_fn      = &silverline_doorbell_callback,
        .disabled       = 0,
        .fields        = output_fields,
};
