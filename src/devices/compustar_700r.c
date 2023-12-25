/** @file
    Compustar 700R - Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can panicistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int compustar_700r_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Compustar 700R - Car Remote

Manufacturer:
- Compustar

Supported Models:
- 700R
- 900R

Data structure:

Compustar 700R Transmitters

The transmitter uses a fixed code message.

Button operation:
This transmitter has 4 buttons which can be held to continuously transmit messages.
Multiple buttons can be held down to send unique codes.

Data layout:

IIIII bbbbb xxx

- I: 20 bit remote ID
- B: 5 bit button flags
- x: 3 bit unknown (always set to 111)

Format string:

ID: hhhhh BUTTON: bbbbb UNKNOWN: bbb

*/

#include "decoder.h"

static int compustar_700r_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int id     = 0;
    int button = 0;

    if (bitbuffer->bits_per_row[0] != 25) {
        return DECODE_ABORT_LENGTH;
    }

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *bytes = bitbuffer->bb[0];

    if ((bytes[4] & 0x7) != 0x0) {
        return DECODE_ABORT_EARLY;
    }

    id     = bytes[0] << 12 | bytes[1] << 4 | bytes[2] >> 4;
    button = ~(bytes[2] << 1 | bytes[3] >> 7) & 0x1f;

    // button flags
    int unlock = (button & 0x2) >> 1;
    int lock   = (button & 0x4) >> 2;
    int trunk  = (button & 0x8) >> 3;
    int start  = (button & 0x10) >> 4;

    /* clang-format off */
    data_t *data = data_make(
            "model",         "model",        DATA_STRING, "Compustar-700R",
            "id",            "device-id",    DATA_INT,    id,
            "button_code",   "Button Code",  DATA_INT,    button,
            "start",         "Start",        DATA_INT,    start,
            "lock",          "Lock",         DATA_INT,    lock,
            "unlock",        "Unlock",       DATA_INT,    unlock,
            "trunk",         "Trunk",        DATA_INT,    trunk,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button_code",
        "start",
        "lock",
        "unlock",
        "trunk",
        NULL,
};

r_device const compustar_700r = {
        .name        = "Compustar 700R Car Remote",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 592,
        .long_width  = 1760,
        .reset_limit = 1740,
        .tolerance   = 467,
        .decode_fn   = &compustar_700r_decode,
        .fields      = output_fields,
};