/** @file
    Siemens 5WY72XX - Car Remote.

    Copyright (C) 2024 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int siemens_5wy72xx_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    Siemens 5WY72XX - Car Remote.
Siemens - Car Remote (315 MHz)

Manufacturer:
- Siemens

Supported Models:
- 5WY72XX, (FCC ID M3N5WY72XX) (OEM for DaimlerChrysler SKREEK CS and RS vehicle platforms.)

Data structure:

The transmitter uses a rolling code message with an unencrypted sequence number.

Button operation:
This transmitter has up to 6 buttons which can be pressed once to transmit a single message.
Multiple buttons can be pressed to send unique codes.

Data layout:

Data is little endian

PPPP IIIIIIII bbbbbbbb SSSS EEEEEEEE CC

- P: 16 bit preamble (not included in XOR checksum)
- c: 32 bit ID
- b: 8 bit button code
- S: 16 bit sequence
- E: 32 bit encrypted
- C: 8 bit XOR of entire payload, except preamble

Format string:

PREAMBLE: hhhh ID: hhhhhhhh BUTTON: bbbbbbbb SEQUENCE: hhhh ENCRYPTED: hhhhhhhh XOR: hh xxxx

*/

#include "decoder.h"

static int siemens_5wy72xx_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] < 113 || bitbuffer->num_rows > 1) {
        return DECODE_ABORT_LENGTH;
    }

    const uint8_t pattern[2] = {0x60, 0x01};
    int offset               = bitbuffer_search(bitbuffer, 0, 0, pattern, 16) + 16;

    uint8_t bytes[12];
    bitbuffer_extract_bytes(bitbuffer, 0, offset, bytes, 96);

    int sum = add_bytes(bytes, 12);
    if (sum == 0 || sum == 0xff * 12) {
        return DECODE_FAIL_SANITY;
    }

    if (xor_bytes(bytes, 12) != 0) {
        return DECODE_FAIL_MIC;
    }

    // parse payload
    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%02X%02X%02X%02X", bytes[3], bytes[2], bytes[1], bytes[0]);
    int button   = bytes[4];
    int sequence = (bytes[5] << 8) | bytes[6];
    char encrypted_str[9];
    snprintf(encrypted_str, sizeof(encrypted_str), "%02X%02X%02X%02X", bytes[10], bytes[9], bytes[8], bytes[7]);

    // parse buttons
    char button_str[64]           = "";
    char const *delimiter         = "; ";
    char const *button_strings[6] = {
            "Lock",       // 0x01
            "Unlock",     // 0x02
            "Trunk",      // 0x04
            "Panic",      // 0x0f
            "Left Door",  // 0x10
            "Right Door", // 0x20
    };

    int matches = 0;
    int mask    = 0x01;
    for (int i = 0; i < 6; i++) {
        if (button & mask) {
            if (matches) {
                strcat(button_str, delimiter);
            }
            strcat(button_str, button_strings[i]);
            matches++;
        };
        mask <<= 1;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",            "model",       DATA_STRING, "Siemens-5WY72XX",
            "id",               "ID",          DATA_STRING, id_str,
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
        "id",
        "encrypted",
        "button_code",
        "button_str",
        "sequence",
        "mic",
        NULL,
};

r_device const siemens_5wy72xx_car_remote = {
        .name        = "Siemens 5WY72XX Car Remote (-f 315.1M)",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 220,
        .reset_limit = 10000,
        .decode_fn   = &siemens_5wy72xx_car_remote_decode,
        .fields      = output_fields,
};
