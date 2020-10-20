/** @file
    Nice Flor-s

    Copyright (C) 2020 Samuel Tardieu <sam@rfc1149.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Nice Flor-s

Protocol description:
The protocol has been analyzed at this link: http://phreakerclub.com/1615
*/

#include "decoder.h"

static int nice_flor_s_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 2 || bitbuffer->bits_per_row[1] != 0) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[0] != 52) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_invert(bitbuffer);
    uint8_t *b = bitbuffer->bb[0];

    uint8_t button_id = b[0] >> 4;
    if (button_id < 1 || button_id > 4) {
        return DECODE_ABORT_EARLY;
    }
    int repeat = (b[0] & 0xf) ^ 0xf ^ button_id;
    char serial[8], code[5];
    // Encrypted serial is made of nibbles 2 and 7 to 12
    snprintf(serial, sizeof serial,
             "%x%x%02x%02x%x",
             b[1] >> 4, b[3] & 0xf, b[4], b[5], b[6] >> 4);
    // Encrypted rolling code is made of nibbles 3 to 6
    snprintf(code, sizeof code,
             "%x%02x%x",
             b[1] & 0xf, b[2], b[3] >> 4);

    /* clang-format off */
    data_t *data = data_make(
            "model",   "",              DATA_STRING, "Nice Flor-s",
            "button",  "Button ID",     DATA_INT,     button_id,
            "serial",  "Serial (enc.)", DATA_FORMAT, "%s", DATA_STRING, serial,
            "code",    "Code (enc.)",   DATA_FORMAT, "%s", DATA_STRING, code,
            "repeat",  "",              DATA_INT,     repeat,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "button",
        "serial",
        "code",
        "repeat",
        NULL,
};

r_device nice_flor_s = {
        .name           = "Nice Flor-s",
        .modulation     = OOK_PULSE_PWM,
        .short_width    = 500 ,   // short pulse is ~500 us + ~1000 us gap
        .long_width     = 1000,   // long pulse is ~1000 us + ~500 us gap
        .sync_width     = 1500,   // sync pulse is ~1500 us + ~1500 us gap
        .gap_limit      = 2000,
        .reset_limit    = 5000,
        .tolerance      = 100,
        .decode_fn      = &nice_flor_s_callback,
        .disabled       = 0,
        .fields         = output_fields,
};
