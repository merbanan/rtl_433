/* Generic remotes and sensors using PT2260/PT2262 SC2260/SC2262 EV1527 protocol
 *
 * Tested devices:
 * SC2260
 * EV1527
 *
 * Copyright (C) 2015 Tommy Vestermark
 * Copyright (C) 2015 nebman
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "decoder.h"

static int generic_remote_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    bitrow_t *bb = bitbuffer->bb;
    uint8_t *b = bb[0];
    char tristate[23];
    char *p = tristate;

    //invert bits, short pulse is 0, long pulse is 1
    b[0] = ~b[0];
    b[1] = ~b[1];
    b[2] = ~b[2];

    unsigned bits = bitbuffer->bits_per_row[0];

    // Validate package
    if ((bits != 25)
            || (b[3] & 0x80) == 0 // Last bit (MSB here) is always 1
            || (b[0] == 0 && b[1] == 0) // Reduce false positives. ID 0x0000 not supported
            || (b[2] == 0)) // Reduce false positives. CMD 0x00 not supported
        return 0;

    int id_16b = b[0] << 8 | b[1];
    int cmd_8b = b[2];

    // output tristate coding
    uint32_t full = b[0] << 16 | b[1] << 8 | b[2];

    for (int i = 22; i >= 0; i -= 2) {
        switch ((full>>i) & 0x03) {
        case 0x00: *p++ = '0'; break;
        case 0x01: *p++ = 'Z'; break; // floating / "open"
        case 0x02: *p++ = 'X'; break; // tristate 10 is invalid code for SC226x but valid in EV1527
        case 0x03: *p++ = '1'; break;
        default:   *p++ = '?'; break; // not possible anyway
        }
    }
    *p = '\0';

    data = data_make(
            "model",        "",             DATA_STRING, _X("Generic-Remote","Generic Remote"),
            "id",           "House Code",   DATA_INT, id_16b,
            "cmd",          "Command",      DATA_INT, cmd_8b,
            "tristate",     "Tri-State",    DATA_STRING, tristate,
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "cmd",
    "tristate",
    NULL
};

r_device generic_remote = {
    .name           = "Generic Remote SC226x EV1527",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 464,
    .long_width     = 1404,
    .reset_limit    = 1800,
    .sync_width     = 0,    // No sync bit used
    .tolerance      = 200, // us
    .decode_fn      = &generic_remote_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
