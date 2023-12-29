/** @file
    6SC0 - Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int six_sc_zero_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
6SC0 - Car Remote (315 MHz)

Manufacturer:
- Unknown

Supported Models:
- 6SC0

Data structure:

The transmitter uses a rolling code message with an unencrypted sequence number.

Button operation:
This transmitter has 4 buttons which can be pressed once to transmit a single message

Data layout:

Bytes are reflected

IIIIIIII bbbb x d xx CC

- I: 32 bit remote ID
- b: 4 bit button code
- x: 1 bit unknown
- d: 1 bit set to 1 when multiple buttons are pressed
- x: 2 bit unknown
- C: 8 bit checksum

Format string:

PREAMBLE: hhhh ENCRYPTED: hhhhhhhh BUTTON: bbbb UNKNOWN: bbbb SEQUENCE: hhhh CHECKSUM: hhhh

*/

#include "decoder.h"

static int six_sc_zero_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 1, 48);

    if (bitbuffer->bits_per_row[0] > 88) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *bytes = bitbuffer->bb[row];

    if (bytes[0] != 0x55 || bytes[1] != 0x54) {
        return DECODE_FAIL_SANITY;
    }

    if (xor_bytes(bytes + 2, 9)) {
        return DECODE_FAIL_MIC;
    }

    // The transmission is LSB first, big endian.
    uint32_t encrypted = ((unsigned)reverse8(bytes[5]) << 24) | (reverse8(bytes[4]) << 16) | (reverse8(bytes[3]) << 8) | (reverse8(bytes[2]));
    int button         = reverse8(bytes[6]) & 0xf;
    int sequence       = ((unsigned)(reverse8(bytes[8]) << 8) | reverse8(bytes[7]));

    char encrypted_str[9];
    snprintf(encrypted_str, sizeof(encrypted_str), "%08X", encrypted);

    char const *button_str;

    /* clang-format off */
    switch (button) {
        case 0x1: button_str = "Unlock"; break;
        case 0x2: button_str = "Lock"; break;
        case 0x3: button_str = "Trunk"; break;
        case 0x4: button_str = "Panic"; break;
        default: button_str = "?"; break;
    }
    /* clang-format on */

    /* clang-format off */
    data_t *data = data_make(
            "model",            "model",       DATA_STRING, "6SC0-CarRemote",
            "encrypted",        "",            DATA_STRING, encrypted_str,
            "button_code",      "Button Code", DATA_INT,    button,
            "button_str",       "Button",      DATA_STRING, button_str,
            "sequence",         "Sequence",    DATA_INT,    sequence,
            "mic",              "Integrity",   DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "encrypted",
        "button_code",
        "button_str",
        "sequence",
        "mic",
        NULL,
};

r_device const six_sc_zero_car_remote = {
        .name        = "6SC0 Car Remote (-f 315.1M -s 1024k)",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 250,
        .reset_limit = 10000,
        .decode_fn   = &six_sc_zero_car_remote_decode,
        .fields      = output_fields,
};