/** @file
    Audiovox - PRO-OE3B Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int audiovox_pro_oe3b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Audiovox - Car Remote

Manufacturer:
- Audiovox

Supported Models:
- PRO-OE3B, AVX01BT3CL3 (FCC ID BGAOE3B)
- PRO-OE4B, AVX01BT3CL3 (FCC ID BGAOE3B)

Data structure:

This transmitter uses a fixed code transmitting on 302.9 MHz.
The same code is continuously repeated while button is held down.
Multiple buttons can be pressed to set multiple button flags.

Data layout:

Bits are inverted.

IIII 110b1b1b 1111

- I: 16 bit ID
- 1: always set to 1
- 0: always set to 0
- b: 3 bit flags indicating button(s) pressed
- 1: always set to 1

Format string:

ID: hhhh x b x TRUNK:b x UNLOCK: b x LOCK: b h

*/

#include "decoder.h"

static int audiovox_pro_oe3b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] != 25) {
        return DECODE_ABORT_LENGTH;
    }

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *bytes = bitbuffer->bb[0];

    if (bytes[2] & 0xaa || bytes[2] == 0x55) {
        return DECODE_FAIL_SANITY;
    }

    bitbuffer_invert(bitbuffer);

    uint16_t id = (bytes[0] << 8) | bytes[1];
    if (id == 0 || id == 0xffff) {
        return DECODE_FAIL_SANITY;
    }

    char id_str[5];
    snprintf(id_str, sizeof(id_str), "%04X", id);

    // parse buttons
    int button = (bytes[2] & 0x40 >> 3) | // Trunk
                 (bytes[2] & 0x10 >> 2) | // Option
                 (bytes[2] & 0x4 >> 1) |  // Unlock
                 (bytes[2] & 0x1);        // Lock

    char button_str[64]           = "";
    char const *delimiter         = "; ";
    char const *button_strings[4] = {
            "Lock",
            "Unlock",
            "Option",
            "Trunk",
    };

    int matches = 0;
    int mask    = 0x01;
    for (int i = 0; i < 4; i++) {
        if (bytes[2] & mask) {
            if (matches) {
                strcat(button_str, delimiter);
            }
            strcat(button_str, button_strings[i]);
            matches++;
        };
        mask <<= 2;
    }

    if (!matches) {
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",       "model",       DATA_STRING, "Audiovox-PROOE3B",
            "id",          "ID",          DATA_STRING, id_str,
            "button_code", "Button Code", DATA_INT,    button,
            "button_str",  "Button",      DATA_STRING, button_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button_code",
        "button_str",
        NULL,
};

r_device const audiovox_pro_oe3b = {
        .name        = "Audiovox PRO-OE3B Car Remote (-f 303M)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 445,
        .long_width  = 895,
        .reset_limit = 1790,
        .gap_limit   = 1790,
        .sync_width  = 1368,
        .decode_fn   = &audiovox_pro_oe3b_decode,
        .priority    = 10,
        .fields      = output_fields,
};