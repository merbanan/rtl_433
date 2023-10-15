/** @file
    Decoder for Regency fan remotes.

    Copyright (C) 2020-2022 David E. Tiller

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decoder for Regency fan remotes.

Regency fans use OOK_PULSE_PPM encoding.
The packet starts with 576 uS start pulse.
- 0 is defined as a 375 uS gap followed by a 970 uS pulse.
- 1 is defined as a 880 uS gap followed by a 450 uS pulse.

Transmissions consist of the start bit followed by bursts of 20 bits.
These packets ar repeated up to 11 times.

As written, the PPM code always interprets a narrow gap as a 1 and a
long gap as a 0, however the actual data over the air is inverted,
i.e. a short gap is a 0 and a long gap is a 1. In addition, the data
is 5 nibbles long and is represented in Little-Endian format. In the
code I invert the bits and also reflect the bytes. Reflection introduces
an additional nibble at bit offsets 16-19, so the data is expressed a 3
complete bytes.

The examples below are _after_ inversion and reflection (MSB's are on
the left).

Packet layout:

     Bit number
     0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23
      CHANNEL  |  COMMAND  |            VALUE       | 0  0  0  0| 4 bit checksum

CHANNEL is determined by the bit switches in the battery compartment. All
switches in the 'off' position result in a channel of 15, implying that the
switches pull the address lines down when in the on position.

COMMAND is one of the following:

- 1 (0x01)
    value: (0xc0, unused).

- 2 (0x02)
    value: 0x01-0x07. On my remote, the speeds are shown as 8 - value.

- 4 (0x04)
    value: 0x00-0xc3. The value is the intensity percentage.
           0x00 is off, 0xc3 is 99% (full).

- 5 (0x05)
    value: 0x00 is 'off', 0x01 is on.

- 6 (0x06)
    value: 0x07 is one way, 0x83 is the other.

The CHECKSUM is calculated by adding the nibbles of the first two bytes
and ANDing the result with 0x0f.

*/

static int regency_fan_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    char const * const command_names[] = {
            /* 0  */ "invalid",
            /* 1  */ "fan_speed",
            /* 2  */ "fan_speed",
            /* 3  */ "invalid",
            /* 4  */ "light_intensity",
            /* 5  */ "light_delay",
            /* 6  */ "fan_direction",
            /* 7  */ "invalid",
            /* 8  */ "invalid",
            /* 9  */ "invalid",
            /* 10 */ "invalid",
            /* 11 */ "invalid",
            /* 12 */ "invalid",
            /* 13 */ "invalid",
            /* 14 */ "invalid",
            /* 15 */ "invalid",
    };

    int return_code = 0;

    bitbuffer_invert(bitbuffer);

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        int num_bits = bitbuffer->bits_per_row[row];

        if (num_bits != 21) { // Max number of bits is 21
            decoder_logf(decoder, 2, __func__, "Expected %d bits, got %d.", 21, num_bits);
            continue;
        }

        uint8_t bytes[3]; // Max number of bytes is 3
        bitbuffer_extract_bytes(bitbuffer, row, 1, bytes, 21); // Valid byte offset is 1, Max number of bits is 21
        reflect_bytes(bytes, 3); // Max number of bytes is 3

        // Calculate nibble sum and compare
        int checksum = add_nibbles(bytes, 2) & 0x0f;
        if (checksum != bytes[2]) { // Sum is in byte 2
            decoder_logf(decoder, 2, __func__, "Checksum failure: expected %0x, got %0x", bytes[2], checksum);
            continue;
        }

        // Now that message "envelope" has been validated, start parsing data.
        int command = bytes[0] >> 4;    // Command and Channel are in byte 0
        int channel = ~bytes[0] & 0x0f; // Command and Channel are in byte 0
        int value   = bytes[1];         // Value is in byte 1

        char value_string[64] = {0};

        switch (command) {
        case 1: // 1 is the command to STOP
            snprintf(value_string, sizeof(value_string), "stop");
            break;

        case 2: // 2 is the command to change fan speed
            snprintf(value_string, sizeof(value_string), "speed %d", value);
            break;

        case 4: // 4 is the command to change the light intensity
            snprintf(value_string, sizeof(value_string), "%d %%", value);
            break;

        case 5: // 5 is the command to set the light delay
            snprintf(value_string, sizeof(value_string), "%s", value == 0 ? "off" : "on");
            break;

        case 6: // 6 is the command to change fan direction
            snprintf(value_string, sizeof(value_string), "%s", value == 0x07 ? "clockwise" : "counter-clockwise");
            break;

        default:
            decoder_logf(decoder, 2, __func__, "Unknown command: %d", command);
            continue;
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",            "",     DATA_STRING,    "Regency-Remote",
                "channel",          "",     DATA_INT,       channel,
                "command",          "",     DATA_STRING,    command_names[command],
                "value",            "",     DATA_STRING,    value_string,
                "mic",              "",     DATA_STRING,    "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return_code++;
    }

    return return_code;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "channel",
        "command",
        "value",
        "mic",
        NULL,
};

r_device const regency_fan = {
        .name        = "Regency Ceiling Fan Remote (-f 303.75M to 303.96M)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 580,
        .long_width  = 976,
        .gap_limit   = 8000,
        .reset_limit = 14000,
        .decode_fn   = &regency_fan_decode,
        .fields      = output_fields,
};
