/** @file
    GM - Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int gm_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
General Motors - Car Remote (315 MHz)

Manufacturer:
- General Motors

Supported Models:
- ABO1502T

Data structure:

The transmitter uses a rolling code message with an unencrypted sequence number.

Button operation:
This transmitter has 2 to 4 buttons which can be pressed once to transmit a single message
Pressing both lock and unlock appears to send a fixed code, possibly a PRNG seed or secret key for the rolling code.

Data layout:

PP xxxx cccc IIIIIIII SSSSSS EEEEEE CC

- P: 8 bit unknown, possibly part of the ID
- c: 4 bit checksum of button code
- b: 4 bit button code
- I: 32 bit ID
- S: 24 bit sequence
- E: 24 bit encrypted
- C: 8 bit checksum of entire payload

Format string:

UNKNOWN: bbbbbbbb BUTTON_CHECKSUM: bbbb BUTTON: bbbb ID: hhhhhhhh SEQUENCE: hhhhhh ENCRYPTED: hhhhhh CHECKSUM: hh

*/

#include "decoder.h"

static int gm_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] < 113 || bitbuffer->num_rows > 1) {
        return DECODE_ABORT_LENGTH;
    }

    // a long wake up payload is sent that may be truncated, so start from the end of the payload.
    int offset = bitbuffer->bits_per_row[0] - 113;

    uint8_t bytes[14];
    bitbuffer_extract_bytes(bitbuffer, 0, offset, bytes, 112);

    // check one byte from the wake up signal
    if (bytes[0] != 0xff) {
        return DECODE_FAIL_SANITY;
    }

    // validate mic
    int button_checksum = add_nibbles(bytes + 2, 1);
    if (button_checksum == 0 || (button_checksum & 0xf) != 0) {
        return DECODE_FAIL_MIC;
    }

    int full_checksum = add_bytes(bytes + 1, 13);
    if (full_checksum == 0 || (full_checksum & 0xff) != 0) {
        return DECODE_FAIL_MIC;
    }

    // parse payload
    int button         = bytes[2] & 0x7;
    uint32_t id        = (bytes[3] << 24) | (bytes[4] << 16) | (bytes[5] << 8) | bytes[6];
    int sequence       = (bytes[7] << 16) | (bytes[8] << 8) | bytes[9];
    uint32_t encrypted = (bytes[10] << 16) | (bytes[11] << 8) | bytes[12];

    char id_str[11];
    snprintf(id_str, sizeof(id_str), "%02X%08X", bytes[1], id);

    char encrypted_str[7];
    snprintf(encrypted_str, sizeof(encrypted_str), "%06X", encrypted);

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
            "model",       "model",       DATA_STRING, "GM-ABO1502T",
            "id",          "ID",          DATA_STRING, id_str,
            "encrypted",   "",            DATA_STRING, encrypted_str,
            "button_code", "Button Code", DATA_INT,    button,
            "button_str",  "Button",      DATA_STRING, button_str,
            "sequence",    "Sequence",    DATA_INT,    sequence,
            "mic",         "Integrity",   DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "encrypted",
        "button_code",
        "button_str",
        "sequence",
        "mic",
        NULL,
};

r_device const gm_car_remote = {
        .name        = "GM ABO1502T Car Remote (-f 314.9M)",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 300,
        .long_width  = 500,
        .reset_limit = 20000,
        .decode_fn   = &gm_car_remote_decode,
        .fields      = output_fields,
};