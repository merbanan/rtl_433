/** @file
    Nexa decoder.

    Copyright (C) 2017 Christian Juncker Brædstrup

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Nexa decoder.
Might be similar to an x1527.
S.a. Kaku, Proove.

Tested devices:
- Magnetic sensor - LMST-606

Packet gap is 10 ms.

This device is very similar to the proove magnetic sensor.
The proove decoder will capture the OFF-state but not the ON-state
since the Nexa uses two different bit lengths for ON and OFF.
*/

#include "decoder.h"

static int nexa_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;

    /* Reject missing sync */
    if (bitbuffer->syncs_before_row[0] != 1)
        return DECODE_ABORT_EARLY;

    /* Reject codes of wrong length */
    if (bitbuffer->bits_per_row[0] != 64 && bitbuffer->bits_per_row[0] != 72)
        return DECODE_ABORT_LENGTH;

    bitbuffer_t databits = {0};
    // note: not manchester encoded but actually ternary
    unsigned pos = bitbuffer_manchester_decode(bitbuffer, 0, 0, &databits, 80);
    bitbuffer_invert(&databits);

    /* Reject codes when Manchester decoding fails */
    if (pos != 64 && pos != 72)
        return DECODE_ABORT_LENGTH;

    uint8_t *b = databits.bb[0];

    uint32_t id        = (b[0] << 18) | (b[1] << 10) | (b[2] << 2) | (b[3] >> 6); // ID 26 bits
    uint32_t group_cmd = (b[3] >> 5) & 1;
    uint32_t on_bit    = (b[3] >> 4) & 1;
    uint32_t channel   = ((b[3] >> 2) & 0x03) ^ 0x03; // inverted
    uint32_t unit      = (b[3] & 0x03) ^ 0x03;        // inverted

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, _X("Nexa-Security","Nexa"),
            "id",            "House Code",  DATA_INT,    id,
            "channel",       "Channel",     DATA_INT,    channel,
            "state",         "State",       DATA_STRING, on_bit ? "ON" : "OFF",
            "unit",          "Unit",        DATA_INT,    unit,
            "group",         "Group",       DATA_INT,    group_cmd,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "state",
        "unit",
        "group",
        NULL,
};

r_device nexa = {
        .name        = "Nexa",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 270,  // 1:1
        .long_width  = 1300, // 1:5
        .sync_width  = 2700, // 1:10
        .tolerance   = 200,
        .gap_limit   = 1500,
        .reset_limit = 2800,
        .decode_fn   = &nexa_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
