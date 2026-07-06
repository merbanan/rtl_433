/** @file
    Continental - Car Remote.

    Copyright (C) 2024 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Continental - Car Remote (313 MHz).

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

    uint32_t id        = (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
    int button         = bytes[5] >> 4;
    int sequence       = (bytes[6] << 16) | (bytes[7] << 8) | bytes[8];
    uint32_t encrypted = (uint32_t)bytes[9] << 24 | (uint32_t)bytes[10] << 16 | (uint32_t)bytes[11] << 8 | bytes[12];

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

/**
Honda Car Keyfob (313/433 MHz).

Manufacturer:
- Honda

Supported Models:
- FCC ID KR5V2X (313.55/314.15 MHz)
- FCC ID KR5V1X (433.66/434.18 MHz)

Note:
This is an alternate decoding of the same physical remotes handled by
continental_car_remote above (same FCC ID KR5V2X).

Data layout:

    MMMMMM HH DDDDDDDD EE NNNNNN RRRRRRRR CC

- M: 24 bit manufacturer ID (sync/preamble)
- H: 8 bit packet index (a button press sends the packet twice; 0x08 then 0x0a)
- D: 32 bit device ID of the keyfob
- E: 8 bit keyfob command (event)
- N: 24 bit counter
- R: 32 bit rolling code
- C: 8 bit CRC, OpenSafety poly 0x2f init 0x00

Format string:

    ID: hhhhhhhh EVENT: hh COUNTER: hhhhhh CODE: hhhhhhhh CHECKSUM: hh

*/

static int honda_keyfob_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows > 1) {
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[0] < 150 || bitbuffer->bits_per_row[0] > 184) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t const preamble[] = {0xec, 0x0f, 0x62}; // Honda keyfob manufacturer code
    unsigned bit_offset      = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8);

    if (bit_offset + 16 + 120 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t b[15];
    // extract 120 bits after the sync, excluding the first two bytes of the manufacturer code
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset + 16, b, 120);

    int crc = crc8(b, 14, 0x2f, 0x00); // OpenSafety CRC-8
    if (crc != b[14]) {
        return DECODE_FAIL_MIC;
    }

    int device_id      = (b[2] << 24) | (b[3] << 16) | (b[4] << 8) | b[5];
    int device_counter = (b[7] << 16) | (b[8] << 8) | b[9];
    int rolling_code   = (b[10] << 24) | (b[11] << 16) | (b[12] << 8) | b[13];

    char const *event_str;
    /* clang-format off */
    switch (b[6]) {
        case 0x21: event_str = "Lock"; break;
        case 0x22: event_str = "Unlock"; break;
        case 0x24: event_str = "Trunk"; break;
        case 0x27: event_str = "Emergency"; break;
        case 0x2d: event_str = "RemoteStart"; break;
        default:   event_str = "?"; break;
    }
    /* clang-format on */

    /* clang-format off */
    data_t *data = data_make(
            "model",       "model",     DATA_STRING, "Honda-KR5V2X1X",
            "id",          "Device ID", DATA_FORMAT, "%08x", DATA_INT, device_id,
            "event",       "Event",     DATA_STRING, event_str,
            "counter",     "Counter",   DATA_FORMAT, "%06x", DATA_INT, device_counter,
            "code",        "Code",      DATA_FORMAT, "%08x", DATA_INT, rolling_code,
            "mic",         "Integrity", DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const honda_keyfob_output_fields[] = {
        "model",
        "id",
        "event",
        "counter",
        "code",
        "mic",
        NULL,
};

r_device const honda_keyfob = {
        .name        = "Honda Keyfob KR5V2X/1X (-f 433.6M -s 1024k)",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 60,
        .long_width  = 120,
        .reset_limit = 1500,
        .decode_fn   = &honda_keyfob_decode,
        .fields      = honda_keyfob_output_fields,
};
