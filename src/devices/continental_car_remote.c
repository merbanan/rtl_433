/** @file
    Continental - Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int continental_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Continental - Car Remote (313 MHz)

Manufacturer:
- Continental

Supported Models:
- 72147-SNA-A01 (FCC ID KR5V2X) (OEM for Honda)

Data structure:

The transmitter uses a rolling with an unencrypted sequence number.

Button operation:
The unlock, lock buttons can be pressed once to transmit a single message.
The trunk, panic buttons will transmit the same code on a short press.
The trunk, panic buttons will transmit the unique code on a long press.
The panic button will repeat the panic code as long as it is held.

Data layout:

The decoder will match on the last 20 bits of the preamble: 0xf0f06

PPPPP IIIIIIII UU bbbb U IIIII EEEEEEEE CC

- P: 20 bit preamble (following a longer wakeup sequence)
- I: 32 bit remote ID
- U: 8 bit unknown
- b: 4 b bit button code
- U: 4 bit unknown
- E: 32 bit encrypted code
- C: 8 XOR of entire payload

Format string:

PREAMBLE: bbbbbbbb bbbbbbbb bbbb ID: hhhhhhhh UNKNOWN: bbbbbbbb BUTTON: bbbb UNKNOWN: bbbb SEQUENCE: hhhhhh CODE: hhhhhhhhhh CHECKSUM: hh

*/

#include <decoder.h>

static int continental_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] < 132) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t pattern[8] = {0xf0, 0xf0, 0x60};
    int offset         = bitbuffer_search(bitbuffer, 0, 0, pattern, 20) + 20;

    if (bitbuffer->bits_per_row[0] - offset < 112) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t bytes[14];
    bitbuffer_extract_bytes(bitbuffer, 0, offset, bytes, 112);

    uint32_t id        = bytes[0] << 24 | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
    int button         = bytes[5] >> 4;
    int sequence       = (bytes[6] << 16) | bytes[7] << 8 | bytes[8];
    uint32_t encrypted = (bytes[9] << 24) | (bytes[10] << 16) | (bytes[11] << 8) | bytes[12];

    if (id == 0 ||
            button == 0 ||
            sequence == 0 ||
            id == 0xfffffff ||
            encrypted == 0xfffffff ||
            sequence == 0xffffff) {
        return DECODE_FAIL_SANITY;
    }

    if (xor_bytes(bytes, 14)) {
        return DECODE_FAIL_MIC;
    }

    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%08X", id);

    char encrypted_str[9];
    snprintf(encrypted_str, sizeof(encrypted_str), "%08X", encrypted);

    char const *button_str;
    /* clang-format off */
    switch (button) {
        case 0x1: button_str = "Lock"; break;
        case 0x3: button_str = "Unlock"; break;
        case 0x9: button_str = "Trunk Long Press"; break;
        case 0xa: button_str = "Trunk/Panic Short Press"; break;
        case 0xb: button_str = "Panic Long Press"; break;
        default: button_str = "?"; break;
    }
    /* clang-format on */

    /* clang-format off */
    data_t *data = data_make(
            "model",       "model",       DATA_STRING, "Continental-KR5V2X",
            "id",          "ID",          DATA_STRING, id_str,
            "encrypted",   "",            DATA_STRING, encrypted_str,
            "sequence",    "Sequence",    DATA_INT,    sequence,
            "button_code", "Button Code", DATA_INT,    button,
            "button_str",  "Button",      DATA_STRING, button_str,
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
        "sequence",
        "button_code",
        "button_str",
        "mic",
        NULL,
};

r_device const continental_car_remote = {
        .name        = "Continental KR5V2X Car Remote (-f 313.8M -s 1024k)",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 100,
        .long_width  = 200,
        .reset_limit = 1500,
        .decode_fn   = &continental_car_remote_decode,
        .fields      = output_fields,
};