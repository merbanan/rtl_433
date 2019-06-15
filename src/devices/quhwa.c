/* Quhwa
 * HS1527
 *
 * Tested devices:
 * QH-C-CE-3V (which should be compatible with QH-832AC),
 * also sold as "1 by One" wireless doorbell
 *
 * Copyright (C) 2016 Ask Jakobsen
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "decoder.h"

static int quhwa_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int r = bitbuffer_find_repeated_row(bitbuffer, 5, 18);
    if (r < 0)
        return 0;

    uint8_t *b = bitbuffer->bb[r];

    b[0] = ~b[0];
    b[1] = ~b[1];
    b[2] = ~b[2];

    if (bitbuffer->bits_per_row[r] != 18
            || (b[1] & 0x03) != 0x03
            || (b[2] & 0xC0) != 0xC0)
        return 0;

    uint32_t id = (b[0] << 8) | b[1];


    data_t *data = data_make(
            "model", "", DATA_STRING, _X("Quhwa-Doorbell","Quhwa doorbell"),
            "id", "ID", DATA_INT, id,
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    NULL
};

r_device quhwa = {
    .name          = "Quhwa",
    .modulation    = OOK_PULSE_PWM,
    .short_width   = 360,  // Pulse: Short 360µs, Long 1070µs
    .long_width    = 1070, // Gaps: Short 360µs, Long 1070µs
    .reset_limit   = 6600, // Intermessage Gap 6500µs
    .gap_limit     = 1200, // Long Gap 1120µs
    .sync_width    = 0,    // No sync bit used
    .tolerance     = 80,   // us
    .decode_fn     = &quhwa_callback,
    .disabled      = 0,
    .fields        = output_fields
};
