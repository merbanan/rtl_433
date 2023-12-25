/** @file
    Compustar 1WG3R-SH - Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int compustar_1wg3r_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Compustar 1WG3R-SH - Car Remote

Manufacturer:
- Compustar

Supported Models:
- 1WG3R-SH
- 1WAMR-1900

Data structure:

Compustar 1WG3R-SH Transmitters

The transmitter uses a fixed code message.

Button operation:
This transmitter has 4 buttons which can be pressed once to transmit a single message
Multiple buttons can be pressed down to send unique codes.

Long Press:
Hold the button combination down for 2.5 seconds to send a long press signal.

Secondary mode:
Press and hold the unlock and the trunk buttons (II & III) at the same time. (press and hold for 2.5 seconds)
The LED will flash slowly indicating the remote is in the secondary mode.
Button presses are batched by the remote when secondary mode is activated.


Data layout:

IIII x bbbbbbbb iiiiiiii z

- I: 16 bit remote ID
- x: 3 bit unknown (always set to 111)
- i: 8 bit inverted button code
- b: 8 bit button code
- z: 1 bit unknown (always set to 0)

Format string:

ID: hhhh UNKNOWN: bbb BUTTON_INVERSE: bbbbbbbb BUTTON: bbbbbbbb UNKNOWN: b

*/

#include "decoder.h"
#include <stdlib.h>

static int compustar_1wg3r_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int rows_data_idx = -1;
    data_t **rows_data = malloc(bitbuffer->num_rows * sizeof(data_t *));

    // loop through all of the rows and only return unique valid results
    // programming mode will send a sequence of key presses all in one message
    int previous_row = -1;
    for (int current_row = 0; current_row < bitbuffer->num_rows; current_row++) {
        uint8_t *bytes = bitbuffer->bb[current_row];

        // reset row count for the row separator
        if (bitbuffer->bits_per_row[current_row] == 5 && (bytes[0] & 0xf8) == 0xf8) {
            previous_row = -1;
            continue;
        }

        if ((bytes[2] & 0xe0) != 0xe0 || (bytes[4] & 1) != 0x0) {
            continue; // DECODE_ABORT_EARLY;
        }

        if ((bytes[0] = 0xff && bytes[1] == 0xff) ||
                (bytes[0] == 0x00 && bytes[1] == 0x00)) {
            continue; // DECODE_FAIL_SANITY;
        }

        int id             = bytes[0] << 8 | bytes[1];
        int button_inverse = (bytes[2] << 3 & 0xff) | bytes[3] >> 5;
        int button         = ((bytes[3] << 3) & 0xff) | bytes[4] >> 5;

        if ((~button_inverse & 0xff) != button) {
            continue; // DECODE_FAIL_MIC;
        }

        // button flags
        int long_press     = (button & 0x10) > 0;
        int secondary_mode = (button & 0x80) > 0;
        int alarm          = 0;
        int unlock         = 0;
        int lock           = 0;
        int trunk          = 0;
        int start          = 0;

        // these are not bit flags
        // the remote maps the button combinations to tbe below table
        /* clang-format off */
        // shared codes between normal and long press
        switch (button & 0xf) {
            case 0x3: lock = 1; unlock = 1; break;
            case 0x5: lock = 1; trunk = 1; break;
            case 0x9: lock = 1; start = 1; break;
            case 0x6: unlock = 1; trunk = 1; break;
            case 0xa: unlock = 1; start = 1; break;
            case 0xc: start = 1; trunk = 1; break;
            case 0xb: lock = 1; unlock = 1; start = 1; break;
            case 0xe: unlock = 1; start = 1; trunk = 1; break;
            case 0xd: lock = 1; start = 1; trunk = 1; break;
        }

        // unique codes between normal and long press
        switch (button & 0x1f) {
            case 0x0f: lock = 1; break;
            case 0x1f: lock = 1; unlock = 1; start = 1; trunk = 1; break;
            case 0x07: unlock = 1; break;
            case 0x17: lock = 1; unlock = 1; trunk = 1; break;
            case 0x02: trunk = 1; break;
            case 0x04: start = 1; break;
            case 0x12: start = 1; break;
            case 0x14: trunk = 1; break;
            case 0x08: start = 1; trunk = 1; long_press = 1; break;
            case 0x18: alarm = 1; break;
        }
        /* clang-format on */

        if (previous_row >= 0 && bitbuffer_compare_rows(bitbuffer, previous_row, current_row, 35)) {
            continue; // duplicate of previous row
        }

        previous_row = current_row;
        rows_data_idx++;
        /* clang-format off */
        rows_data[rows_data_idx] = data_make(
                "model",            "model",            DATA_STRING, "Compustar-1WG3R-SH",
                "id",               "device-id",        DATA_INT,    id,
                "button_code",      "Button Code",      DATA_INT,    button,
                "alarm",            "Alarm",            DATA_INT,    alarm,
                "start",            "Start",            DATA_INT,    start,
                "lock",             "Lock",             DATA_INT,    lock,
                "unlock",           "Unlock",           DATA_INT,    unlock,
                "trunk",            "Trunk",            DATA_INT,    trunk,
                "long_press",       "Long Press",       DATA_INT,    long_press,
                "secondary_mode",   "Secondary Mode",   DATA_INT,    secondary_mode,
                "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */
    }

    // send the messages in order
    for (int i = 0; i <= rows_data_idx; i++) {
        decoder_output_data(decoder, rows_data[i]);
    }

    free(rows_data);

    if (rows_data_idx < 0) {
        return DECODE_FAIL_OTHER;
    }

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button_code",
        "alarm",
        "start",
        "lock",
        "unlock",
        "trunk",
        "long_press",
        "secondary_mode",
        "mic",
        NULL,
};

r_device const compustar_1wg3r = {
        .name        = "Compustar 1WG3R-SH Car Remote",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 708,
        .long_width  = 1076,
        .reset_limit = 1532,
        .sync_width  = 1448,
        .decode_fn   = &compustar_1wg3r_decode,
        .fields      = output_fields,
};