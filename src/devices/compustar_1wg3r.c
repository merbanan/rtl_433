/** @file
    Compustar 1WG3R - Car Remote.

    Copyright (C) 2024 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int compustar_1wg3r_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Compustar 1WG3R - Car Remote

Manufacturer:
- Compustar

Supported Models:
- 1WG3R-SH
- 1WAMR-1900

Data structure:

Compustar 1WG3R Transmitters

The transmitter uses a fixed code message.

Button operation:
This transmitter has 4 buttons which can be pressed once to transmit a single message
Multiple buttons can be pressed down to send unique codes.

Panic:
Press and hold the lock button for 3 seconds.

Long Press:
Hold the button combination down for 2.5 seconds to send a long press signal.

Secondary mode:
Press and hold the unlock and the trunk buttons (II & III) at the same time. (press and hold for 2.5 seconds)
The LED will flash slowly indicating the remote is in the secondary mode.
Button presses sent in batches by the remote when secondary mode is activated.

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
#include "fatal.h"
#include <stdlib.h>

static int compustar_1wg3r_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int rows_data_idx  = -1;
    data_t **rows_data = malloc(bitbuffer->num_rows * sizeof(data_t *));
    if (!rows_data) {
        WARN_MALLOC("compustar_1wg3r_decode()");
        return -1; // NOTE: returns error on alloc failure.
    }

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

        if ((bytes[0] == 0xff && bytes[1] == 0xff) ||
                (bytes[0] == 0x00 && bytes[1] == 0x00)) {
            continue; // DECODE_FAIL_SANITY;
        }

        uint16_t id = (bytes[0] << 8) | bytes[1];
        char id_str[5];
        snprintf(id_str, sizeof(id_str), "%04X", id);

        int button_inverse = ((bytes[2] << 3) & 0xff) | bytes[3] >> 5;
        int button         = ((bytes[3] << 3) & 0xff) | bytes[4] >> 5;

        if ((~button_inverse & 0xff) != button) {
            continue; // DECODE_FAIL_MIC;
        }

        // parse button
        char button_str[128] = "";

        typedef struct {
            const char *name;
            const uint8_t len;
            const uint8_t *vals;
        } Button;

        /* clang-format off */
        const Button button_map[6] = {
            { .name = "Lock",       .len = 13,  .vals = (uint8_t[]){ 0x03, 0x05, 0x09, 0x0b, 0x0d, 0x0f, 0x1f, 0x17, 0x13, 0x15, 0x19, 0x1b, 0x1d } },
            { .name = "Panic",      .len = 1,   .vals = (uint8_t[]){ 0x18 } },
            { .name = "Start",      .len = 16,  .vals = (uint8_t[]){ 0x09, 0x0a, 0x0c, 0x0b, 0x0e, 0x0d, 0x04, 0x1f, 0x08, 0x19, 0x1a, 0x1c, 0x1b, 0x1e, 0x1d, 0x12 } },
            { .name = "Trunk",      .len = 15,  .vals = (uint8_t[]){ 0x05, 0x06, 0x0c, 0x0e, 0x0d, 0x1f, 0x17, 0x02, 0x15, 0x16, 0x1c, 0x1e, 0x1d, 0x08, 0x14 } },
            { .name = "Unlock",     .len = 13,  .vals = (uint8_t[]){ 0x03, 0x06, 0x0a, 0x0b, 0x0e, 0x1f, 0x07, 0x17, 0x13, 0x16, 0x1a, 0x1b, 0x1e } },
            { .name = "Long Press", .len = 28,  .vals = (uint8_t[]){ 0x23, 0x31, 0x13, 0x16, 0x17, 0x1a, 0x1b, 0x1e, 0x15, 0x16, 0x1c, 0x1e, 0x1d, 0x08, 0x14, 0x08, 0x19, 0x1a, 0x1c, 0x1b, 0x1e, 0x1d, 0x12, 0x13, 0x15, 0x19, 0x1b, 0x1d } },
        };
        /* clang-format on */
        const char *delimiter = "; ";
        const char *unknown   = "?";

        int matches = 0;
        // iterate over the button-to-value map to record which button(s) are pressed
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < button_map[i].len; j++) {
                if ((button & 0x7f) == button_map[i].vals[j]) { // if the button values matches the value in the map
                    if (matches) {
                        strcat(button_str, delimiter); // append a delimiter if there are multiple buttons matching
                    }
                    strcat(button_str, button_map[i].name); // append the button name
                    matches++;                              // record the match
                    break;                                  // move to the next button
                }
            }
        }

        if (!matches) {
            strcat(button_str, unknown);
        }

        if (button & 0x80) {
            if (matches) {
                strcat(button_str, delimiter); // append a delimiter if there are multiple buttons matching
            }
            strcat(button_str, "Secondary Mode"); // append the button name
        }

        if (previous_row >= 0 && bitbuffer_compare_rows(bitbuffer, previous_row, current_row, 35)) {
            continue; // duplicate of previous valid message
        }

        previous_row = current_row;
        rows_data_idx++;
        /* clang-format off */
        rows_data[rows_data_idx] = data_make(
                "model",        "model",       DATA_STRING, "Compustar-1WG3R",
                "id",           "ID",          DATA_STRING, id_str,
                "button_code",  "Button Code", DATA_INT,    button,
                "button_str",   "Button",      DATA_STRING, button_str,
                "mic",          "Integrity",   DATA_STRING, "CHECKSUM",
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
        "button_str",
        "mic",
        NULL,
};

r_device const compustar_1wg3r = {
        .name        = "Compustar 1WG3R Car Remote",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 708,
        .long_width  = 1076,
        .reset_limit = 1532,
        .sync_width  = 1448,
        .decode_fn   = &compustar_1wg3r_decode,
        .fields      = output_fields,
};
