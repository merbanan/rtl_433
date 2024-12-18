/** @file
    Chrysler - Car Remote.

    Copyright (C) 2024 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int chrysler_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Chrysler - Car Remote (315 MHz)

Manufacturer:
- Chrysler

Supported Models:
- 56008761
- 56008762 (FCC ID GQ43VT7T)
- 04686366
- 56021903AA

Data structure:

The transmitter uses a fixed code message.

Button operation:
This transmitter has 3 buttons which can be pressed once to transmit a single message
Multiple buttons can be pressed down to send unique codes.

row | data       | bits
1   | [preamble] | 25
2   | [packet]   | 49
3   | [packet]   | 48

Data layout:

Bytes are inverted and reflected

IIIIIIII bbbb x d xx CC
- I: 32 bit remote ID
- b: 4 bit button code
- x: 1 bit unknown
- d: 1 bit set to 1 when multiple buttons are pressed
- x: 2 bit unknown
- C: 8 bit checksum

Format string:

ID: hhhhhhhh BUTTON: bbbb x MULTIPLE: b xx CHECKSUM: bbbbbbbb

*/

#include "decoder.h"

static int chrysler_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int events = 0;

    bitbuffer_invert(bitbuffer);

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        int offset;

        if (bitbuffer->bits_per_row[row] >= 49) {
            // try 49 bits from the end
            offset = 49;
        }
        else if (bitbuffer->bits_per_row[row] == 48) {
            // try 48 bits from the end
            offset = 48;
        }
        else {
            continue; // return DECODE_ABORT_LENGTH;
        }

        uint8_t bytes[6];
        bitbuffer_extract_bytes(bitbuffer, row, bitbuffer->bits_per_row[row] - offset, bytes, 48);
        reflect_bytes(bytes, 6);

        int sum = add_bytes(bytes, 5);
        if ((sum & 0xff) != bytes[5]) {
            continue; // return DECODE_FAIL_MIC;
        }

        if (sum == 0 || sum == 0xff * 5) {
            continue; // return DECODE_FAIL_SANITY;
        }

        // parse id
        uint32_t id = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
        char id_str[9];
        snprintf(id_str, sizeof(id_str), "%08X", id);

        // parse button
        int button      = bytes[4] >> 4;
        int multi_press = (bytes[4] & 0x4) != 0;

        char button_str[64]           = "";
        char const *delimiter         = "; ";
        char const *button_strings[3] = {
                "Unlock",
                "Lock",
                "Panic"};

        int matches = 0;
        int mask    = 0x01;
        for (int i = 0; i < 3; i++) {
            if (button & mask) {
                if (matches) {
                    strcat(button_str, delimiter);
                }
                strcat(button_str, button_strings[i]);
                matches++;
            };
            mask <<= 1;
        }

        if (!matches || (matches > 1 && !multi_press) || (matches == 1 && multi_press)) {
            continue; // return DECODE_FAIL_SANITY;
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",       "model",       DATA_STRING, "Chrysler-CarRemote",
                "id",          "ID",          DATA_STRING, id_str,
                "button_code", "Button Code", DATA_INT,    button,
                "button_str",  "Button",      DATA_STRING, button_str,
                "mic",         "Integrity",   DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        events++;
    }

    return events;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button_code",
        "button_str",
        "mic",
        NULL,
};

r_device const chrysler_car_remote = {
        .name        = "Chrysler Car Remote (-f 315.1M -s 920k)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 350,      // 1x TE
        .long_width  = 350 * 2,  // 2x TE
        .reset_limit = 350 * 50, // 50x TE
        .sync_width  = 350 * 21, // 22X TE
        .gap_limit   = 350 * 12, // 12x TE
        .tolerance   = 100,
        .decode_fn   = &chrysler_car_remote_decode,
        .fields      = output_fields,
};
