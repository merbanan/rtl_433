/** @file
    Astrostart 2000 - Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can panicistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int astrostart_2000_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Astrostart 2000 - Car Remote

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
    int id       = 0;
    int button   = 0;

    // button flags
    int panic         = 0;
    int start         = 0;
    int stop          = 0;
    int lock          = 0;
    int unlock        = 0;
    int trunk         = 0;
    int three_buttons = 0;

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

    button = bytes[0];
    id     = bytes[2] << 24 | bytes[3] << 16 | bytes[4] << 8 | bytes[5];

    int actual_checksum   = bytes[6] >> 4 & 0xf;
    int expected_checksum = 0;

    for (int i = 2; i < 6; i++) {
        expected_checksum = (expected_checksum + (bytes[i] >> 4)) & 0xf;
        expected_checksum = (expected_checksum + bytes[i]) & 0xf;
    }

    if (actual_checksum != expected_checksum) {
        return DECODE_FAIL_MIC;
    }

    // these are not bit flags
    // the remote appears to map the button combinations to tbe below table
    /* clang-format off */
    switch (button) {
        case 0x2b: lock = 1; break;
        case 0x1f: panic = 1; break;
        case 0x13: start = 1; break;
        case 0x2f: stop = 1; break;
        case 0x23: trunk = 1; break;
        case 0x0b: unlock = 1; break;
        case 0x35: panic = 1; lock = 1; break;
        case 0x0d: panic = 1; stop = 1; break;
        case 0x25: panic = 1; trunk = 1; break;
        case 0x15: panic = 1; unlock = 1; break;
        case 0x37: start = 1; lock = 1; break;
        case 0x2d: start = 1; panic = 1; break;
        case 0x33: start = 1; stop = 1; break;
        case 0x3d: start = 1; trunk = 1; break;
        case 0x3b: start = 1; unlock = 1; break;
        case 0x03: stop = 1; lock = 1; break;
        case 0x1d: stop = 1; trunk = 1; break;
        case 0x17: stop = 1; unlock = 1; break;
        case 0x27: trunk = 1; lock = 1; break;
        case 0x07: trunk = 1; unlock = 1; break;
        case 0x0f: unlock = 1; lock = 1; break;
        case 0x3f: three_buttons = 1; break;
        // default: unknown button
    }
    /* clang-format on */

    /* clang-format off */
    data_t *data = data_make(
            "model",         "model",        DATA_STRING, "Astrostart-2000",
            "id",            "device-id",    DATA_INT,    id,
            "button_code",   "Button Code",  DATA_INT,    button,
            "panic",         "Panic",        DATA_INT,    panic,
            "start",         "Start",        DATA_INT,    start,
            "stop",          "Stop",         DATA_INT,    stop,
            "lock",          "Lock",         DATA_INT,    lock,
            "unlock",        "Unlock",       DATA_INT,    unlock,
            "trunk",         "Trunk",        DATA_INT,    trunk,
            "multiple",      "Multiple",     DATA_INT,    three_buttons,
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
        "panic",
        "start",
        "stop",
        "lock",
        "unlock",
        "trunk",
        "multiple",
        "mic",
        NULL,
};

r_device const astrostart_2000 = {
        .name        = "Astrostart 2000 Car Remote",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 326,
        .long_width  = 526,
        .reset_limit = 541,
        .gap_limit   = 541,
        .tolerance   = 80,
        .decode_fn   = &astrostart_2000_decode,
        .fields      = output_fields,
};