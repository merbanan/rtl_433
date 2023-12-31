/** @file
    Nutek - Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int nutek_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Nutek - Car Remote

Manufacturer:
- Nutek

Supported Models:
- ATCD-1, APS99BT3BCF4, ATCH (FCC ID ELVATCD)
- AVX1BS4, AVX-1BS4 (FCC ID ELVATCC)
- A1BTX (FCC ID ELVATFE)
- 105BP (FCC ID ELVATJA)

Data structure:

Nutek Type 4 and Code Alarm Type 7 Transmitters

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

static int nutek_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint16_t id   = 0;
    uint16_t code = 0;
    int button    = 0;

    if (bitbuffer->bits_per_row[0] != 37) {
        return DECODE_ABORT_LENGTH;
    }

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *bytes = bitbuffer->bb[0];

    id = (bytes[0] << 8) | bytes[1];
    char id_str[5];
    snprintf(id_str, sizeof(id_str), "%04X", id);

    code = (bytes[2] << 8) | bytes[3];
    char code_str[5];
    snprintf(code_str, sizeof(code_str), "%04X", code);

    button = (bytes[4] >> 3) & 0xf;

    if (id == 0 || code == 0 || button == 0 || id == 0xffff || code == 0xffff) {
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",    "model",  DATA_STRING, "Nutek-CarRemote",
            "id",       "ID",     DATA_STRING, id_str,
            "code",     "code",   DATA_STRING, code_str,
            "button",   "button", DATA_INT,    button,
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

r_device const nutek_car_remote = {
        .name        = "Nutek Car Remote",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 500,
        .long_width  = 945,
        .reset_limit = 20000,
        .gap_limit   = 4050,
        .sync_width  = 2000,
        .decode_fn   = &nutek_decode,
        .priority    = 10,
        .fields      = output_fields,
};