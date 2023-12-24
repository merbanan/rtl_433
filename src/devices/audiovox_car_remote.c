/** @file
    Audiovox - Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int audiovox_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Audiovox - Car Remote

Manufacturer:
- Audiovox

Supported Models:
- ATCD-1
- AVX1BS4, AVX-1BS4 (FCC ID ELVATCC)
- A1BTX (FCC ID ELVATFE)
- 105BP (FCC ID ELVATJA)

Data structure:

Audiovox Type 4 and Code Alarm Type 7 Transmitters

Transmitter uses a rolling code that changes between each button press.
The same code is continuously repeated while button is held down.

On some models, multiple buttons can be pressed to set multiple button flags.

Data layout:

IIII CCCC X B

- I: 16 bit ID
- C: 16 bit rolling code, likely encrypted using symmetric encryption
- X: 1 bit unknown, possibly a parity for the decoded rolling code
- B: 4 bit flags indicating button(s) pressed

Format string:

ID: hhhh CODE: hhhh UNKNOWN: x BUTTON: bbbb

*/

#include "decoder.h"

static int audiovox_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int id     = 0;
    int code   = 0;
    int button = 0;

    if (bitbuffer->bits_per_row[0] != 37) {
        return DECODE_ABORT_LENGTH;
    }

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *bytes = bitbuffer->bb[0];

    id     = (bytes[0] << 8) | bytes[1];
    code   = (bytes[2] << 8) | bytes[3];
    button = (bytes[4] >> 3) & 0xf;

    if (id == 0 || code == 0 || button == 0) {
        return DECODE_ABORT_EARLY;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",    "model",        DATA_STRING, "Audiovox-CarRemote",
            "id",       "device-id",    DATA_INT,    id,
            "code",     "code",         DATA_INT,    code,
            "button",   "button",       DATA_INT,    button,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "code",
        "button",
        NULL,
};

r_device const audiovox_car_remote = {
        .name        = "Audiovox car remote",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 550,
        .long_width  = 550,
        .reset_limit = 1300,
        .decode_fn   = &audiovox_decode,
        .fields      = output_fields,
};