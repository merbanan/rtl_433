/** @file
    Nidec - Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int nidec_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Nidec - Car Remote (313 MHz)

Manufacturer:
- Nidec

Supported Models:
- OUCG8D-344H-A (OEM for Honda)

Data structure:

The transmitter uses a rolling code message.

Button operation:
The unlock, lock buttons can be pressed once to transmit a single message.
The trunk, panic buttons will transmit the same code on a short press.
The trunk, panic buttons will transmit the unique code on a long press.
The panic button will repeat the panic code as long as it is held.

Data layout:

Bytes are inverted.

The decoder will match on the last 64 bits of the preamble: 0xfffffff0

SSSS IIIIII uuuu bbbb CC

- I: 16 bit sequence that increments on each code transmitted
- I: 24 bit remote ID
- u: 4 bit unknown
- b: 4 bit button code
- C: 16 bit unknown code, possibly a checksum or rolling code

Format string:

SEQUENCE hhhh ID: hhhhhh UNKNOWN: bbbb BUTTON: bbbb CODE: hhhh

*/

#include <decoder.h>

static int nidec_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] < 128) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t pattern[8] = {0xff, 0xff, 0xff, 0xf0};
    int offset         = bitbuffer_search(bitbuffer, 0, 0, pattern, 32) + 32;

    if (bitbuffer->bits_per_row[0] - offset < 56) {
        return DECODE_ABORT_EARLY;
    }

    bitbuffer_invert(bitbuffer);

    uint8_t bytes[8];
    bitbuffer_extract_bytes(bitbuffer, 0, offset, bytes, 64);

    int sequence  = (bytes[0] << 8) | bytes[1];
    uint32_t id   = (bytes[2] << 16) | (bytes[3] << 8) | bytes[4];
    int button    = bytes[5] & 0xf;
    uint16_t code = (bytes[6] << 8) | bytes[7];

    if (id == 0 ||
            button == 0 ||
            sequence == 0 ||
            id == 0xffffff ||
            sequence == 0xffff) {
        return DECODE_FAIL_SANITY;
    }

    char id_str[7];
    snprintf(id_str, sizeof(id_str), "%06X", id);

    char code_str[5];
    snprintf(code_str, sizeof(code_str), "%04X", code);

    char const *button_str;
    /* clang-format off */
    switch (button) {
        case 0x3: button_str = "Lock"; break;
        case 0x4: button_str = "Unlock"; break;
        case 0x5: button_str = "Trunk/Panic Short Press"; break;
        case 0x6: button_str = "Panic Long Press"; break;
        case 0xf: button_str = "Trunk Long Press"; break;
        default: button_str = "?"; break;
    }
    /* clang-format on */

    /* clang-format off */
    data_t *data = data_make(
            "model",       "model",       DATA_STRING, "Nidec-OUCG8D",
            "id",          "ID",          DATA_STRING, id_str,
            "code",        "",            DATA_STRING, code_str,
            "sequence",    "Sequence",    DATA_INT,    sequence,
            "button_code", "Button Code", DATA_INT,    button,
            "button_str",  "Button",      DATA_STRING, button_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "code",
        "sequence",
        "button_code",
        "button_str",
        NULL,
};

r_device const nidec_car_remote = {
        .name        = "Nidec Car Remote (-f 313.8M -s 1024k)",
        .modulation  = FSK_PULSE_PWM,
        .short_width = 250,
        .long_width  = 500,
        .reset_limit = 1000,
        .decode_fn   = &nidec_car_remote_decode,
        .fields      = output_fields,
};