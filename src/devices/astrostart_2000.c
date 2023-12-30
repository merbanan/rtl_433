/** @file
    Astrostart 2000 - Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int astrostart_2000_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Astrostart 2000 - Car Remote 372.5 MHz

Manufacturer:
- Astroflex

Supported Models:
- Astrostart 2000 (FCC ID J5F-TX2000)
- Astrostart 3000 (FCC ID J5F-TX2000)

Data structure:

Astrostart 2000, 3000 Transmitters

The transmitter uses a fixed code message. Each button press will always send three messages.

Button operation:
This transmitter has 5 (Astrostart 2000) or 6 (Astrostart 3000) buttons.
One or two buttons at a time can be pressed and held to send a unique code.
Pressing three buttons will result in a code, but is not unique to different button combinations.

Using the primary / secondary serial number:

The transmitter supports two sending two serial numbers.
Press and hold a button combination once to use the primary serial number.

The second serial number can be used by pressing the buttons in the below sequence:
1. Press a button or button combination twice, holding the combinations on the second press.
2. Hold the buttons down until you hear the four beeps / see the led flash slowly four times.

Note: The panic button will always send two messages on the primary serial number, and one message on the secondary number.

Data layout:

B X IIII cccc

- B: 8 bit button code
- X: 8 bit inverse of the button code
- I: 32 bit remote id
- c: 4 bit checksum of remote id

Format string:

BUTTON: bbbbbbbb INVERSE: bbbbbbbb ID: hhhhhhhh CHECKSUM: h

*/

#include "decoder.h"

static int astrostart_2000_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] != 52) {
        return DECODE_ABORT_LENGTH;
    }

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *bytes = bitbuffer->bb[0];

    if (bytes[0] != (~bytes[1] & 0xff)) {
        return DECODE_FAIL_MIC;
    }

    int actual_checksum   = bytes[6] >> 4;
    int expected_checksum = 0;

    for (int i = 2; i < 6; i++) {
        expected_checksum = (expected_checksum + (bytes[i] >> 4)) & 0xf;
        expected_checksum = (expected_checksum + bytes[i]) & 0xf;
    }

    if (actual_checksum != expected_checksum) {
        return DECODE_FAIL_MIC;
    }

    // parse id
    uint32_t id = bytes[2] << 24 | bytes[3] << 16 | bytes[4] << 8 | bytes[5];
    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%08X", id);

    // parse button
    int button          = bytes[0];
    char button_str[64] = "";

    typedef struct {
        const char *name;
        const uint8_t len;
        const uint8_t vals[6];
    } S;

    /* clang-format off */
    const S button_map[7] = {
        { .name = "Lock",     .len = 6, .vals = { 0x2b, 0x03, 0x27, 0x0f, 0x35, 0x37 } },
        { .name = "Panic",    .len = 6, .vals = { 0x1f, 0x35, 0x0d, 0x25, 0x15, 0x2d } },
        { .name = "Start",    .len = 6, .vals = { 0x13, 0x37, 0x2d, 0x33, 0x3d, 0x3b } },
        { .name = "Stop", .    len = 6, .vals = { 0x2f, 0x0d, 0x33, 0x03, 0x1d, 0x17 } },
        { .name = "Trunk",    .len = 6, .vals = { 0x23, 0x25, 0x3d, 0x1d, 0x27, 0x07 } },
        { .name = "Unlock",   .len = 6, .vals = { 0x0b, 0x15, 0x3b, 0x17, 0x07, 0x0f } },
        { .name = "Multiple", .len = 1, .vals = { 0x3F } }
    };
    /* clang-format on */
    const char *delimiter = "; ";
    const char *unknown   = "?";

    int matches = 0;
    // iterate over the button to value map to record which button(s) are pressed
    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < button_map[i].len; j++) {
            if (button == button_map[i].vals[j]) { // if the button values matches the value in the map
                if (matches) {
                    strcat(button_str, delimiter); // append a delimiter if there are multiple buttons matching
                }
                strcat(button_str, button_map[i].name); // append the button name
                matches++; // record the match
                break; // move to the next button
            }
        }
    }

    if (!matches) {
        strcat(button_str, unknown);
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",         "model",        DATA_STRING, "Astrostart-2000",
            "id",            "ID",           DATA_STRING, id_str,
            "button_code",   "Button Code",  DATA_INT,    button,
            "button_str",    "Button",       DATA_STRING, button_str,
            "mic",           "Integrity",    DATA_STRING, "CHECKSUM",
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
        "mic",
        NULL,
};

r_device const astrostart_2000 = {
        .name        = "Astrostart 2000 Car Remote (-f 372.5M)",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 326,
        .long_width  = 526,
        .reset_limit = 541,
        .gap_limit   = 541,
        .tolerance   = 80,
        .decode_fn   = &astrostart_2000_decode,
        .fields      = output_fields,
};