
/** @file
    Alps FWB1U545 - Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int alps_fwb1u545 _car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Alps FWB1U545 - Car Remote.

Manufacturer:
- Alps Electric

Supported Models:
- FWB1U545, (FCC ID CWTWB1U545) (OEM for Honda)

Data structure:

The transmitter uses a fixed code an unencrypted sequence number.

Button operation:
This transmitter has up to 4 buttons which can be pressed once to transmit a single message.

Data layout:

Data is little endian

PP IIIIIIII bbbbbbbb bbbbbbbb SSSS CC

- P: 8 bit preamble
- I: 32 bit ID
- b: 8 bit button code
- b: 8 bit button code (copy)
- S: 16 bit sequence
- C: 4 bit unknown, maybe checksum or crc

Format string:

PREAMBLE: bbbbbbbb ID: hhhhhhhh BUTTON: bbbbbbbb BUTTON_XOR: bbbbbbbb SEQUENCE: hhhh UNKNOWN: bbbb

*/

#include "decoder.h"

static int alps_fwb1u545_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] != 76 || bitbuffer->num_rows > 1) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *bytes = bitbuffer->bb[0];

    // check preamble and button XOR
    if (bytes[0] != 0x55 || bytes[5] != bytes[6]) {
        return DECODE_FAIL_SANITY;
    }

    // parse payload
    uint32_t id = (bytes[1] << 24) | (bytes[2] << 16) | (bytes[3] << 8) | bytes[4];
    if (id == 0 || id == 0xffffffff) {
        return DECODE_FAIL_SANITY;
    }

    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%08X", id);

    int button   = (bytes[5] & 0xf0) >> 4;
    int sequence = (bytes[7] << 8) | bytes[8];

    // map buttons
    const char *button_str;
    /* clang-format off */
    switch (button) {
        case 0xe: button_str = "Lock"; break;
        case 0xc: button_str = "Panic"; break;
        case 0x5: button_str = "Panic Held"; break;
        case 0x1: button_str = "Unlock"; break;
        default: button_str  = "?"; break;
    }
    /* clang-format on */

    /* clang-format off */
    data_t *data = data_make(
            "model",            "model",       DATA_STRING, "Alps-FWB1U545",
            "id",               "ID",          DATA_STRING, id_str,
            "button_code",      "Button Code", DATA_INT,    button,
            "button_str",       "Button",      DATA_STRING, button_str,
            "sequence",         "Sequence",    DATA_INT,    sequence,
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
        "sequence",
        NULL,
};

r_device const alps_fwb1u545_car_remote = {
        .name        = "Alps FWB1U545 Car Remote",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 500,
        .reset_limit = 1500,
        .decode_fn   = &alps_fwb1u545_car_remote_decode,
        .fields      = output_fields,
};