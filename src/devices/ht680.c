/** @file
    HT680 based Remote control (broadly similar to x1527 protocol).

    Copyright (C) 2016 Igor Polovnikov

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
HT680 based Remote control (broadly similar to x1527 protocol).

- short is 850 us gap 260 us pulse
- long is 434 us gap 663 us pulse

*/

#include "decoder.h"

static int ht680_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t b[5]; // 36 bits

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        if (bitbuffer->bits_per_row[row] != 41 || // Length of packet is 41 (36+5)
                (bitbuffer->bb[row][0] & 0xf8) != 0xa8) // Sync is 10101xxx (5 bits)
        continue; // DECODE_ABORT_LENGTH

        // remove the 5 sync bits
        bitbuffer_extract_bytes(bitbuffer, row, 5, b, 36);

        if ((b[1] & 0xf0) != 0xa0 && // A4, A5 always "open" on HT680
            (b[2] & 0x0c) != 0x08 && // AD10 always "open" on HT680
            (b[3] & 0x30) != 0x20 && // AD13 always "open" on HT680
            (b[4] & 0xf0) != 0xa0) // AD16, AD17 always "open" on HT680
        continue; // DECODE_ABORT_EARLY

        // Tristate coding
        char tristate[21];
        char *p = tristate;
        for (int byte = 0; byte < 5; byte++) {
            for (int bit = 7; bit > 0; bit -= 2) {
                switch ((b[byte] >> (bit-1)) & 0x03) {
                    case 0x00: *p++ = '0'; break;
                    case 0x01: *p++ = 'X'; break; // Invalid code 01
                    case 0x02: *p++ = 'Z'; break; // Floating state Z is 10
                    case 0x03: *p++ = '1'; break;
                    default: *p++ = '?'; break; // Unknown error
                }
            }
        }
        // remove last two unused bits
        p -= 2;
        *p = '\0';

        int address = (b[0]<<12) | (b[1]<<4) | b[2] >> 4;
        int button1 = (b[3]>>0) & 0x03;
        int button2 = (b[3]>>2) & 0x03;
        int button3 = (b[3]>>6) & 0x03;
        int button4 = (b[2]>>0) & 0x03;

        /* clang-format off */
        data = data_make(
                "model",    "",                 DATA_STRING, _X("HT680-Remote","HT680 Remote control"),
                _X("id","address"),  "Address",          DATA_FORMAT, "0x%06X", DATA_INT, address,
                "button1",  "Button 1",         DATA_STRING, button1 == 3 ? "PRESSED" : "",
                "button2",  "Button 2",         DATA_STRING, button2 == 3 ? "PRESSED" : "",
                "button3",  "Button 3",         DATA_STRING, button3 == 3 ? "PRESSED" : "",
                "button4",  "Button 4",         DATA_STRING, button4 == 3 ? "PRESSED" : "",
                "tristate", "Tristate code",    DATA_STRING, tristate,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return 0;
}

static char *output_fields[] = {
        "model",
        "id",
        "address", // TODO: remove this
        "button1",
        "button2",
        "button3",
        "button4",
        "tristate",
        NULL,
};

r_device ht680 = {
        .name        = "HT680 Remote control",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 200,
        .long_width  = 600,
        .gap_limit   = 1200,
        .reset_limit = 14000,
        .decode_fn   = &ht680_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
