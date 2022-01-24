/** @file
    Decoder for Heatilator gas log remotes.

    Copyright (C) 2020-2022 David E. Tiller

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decoder for Heatilator gas log remotes.

Heatilator gas logs use OOK_PULSE_PPM encoding. The format is very similar to
that decoded by 'generic_remote', but seems to differ slightly in timing. The
device does _not_ use a discrete chip to generate the waveform; it's generated
in code.

The packet starts with 380 uS start pulse followed by an eternity (14.3 mS) of silence.
- 0 is defined as a 1430 uS pulse followed by a 460 uS gap.
- 1 is defined as a 380 uS pulse followed by a 1420 uS gap.

Transmissions consist of the start bit followed by 24 data bits. These packets are
repeated many times.

Because there's such a long start bit/preamble, the decoder usually creates the first
row with a single bit, followed by 'n' rows with 25 bits (the 24 data bits and the
start bit of the following packet), then the last row with the expected 24 bits.

Packet layout:

     Bit number
     0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23
     - - - - - - - - - - DEVICE SERIAL NUMBER - - - - - - - - - |- COMMAND -

The device serial number is (presumedly) burned into the device when manufactured.
The command is further broken down into the following bits:

    20 21 22 23
    X  X  S  T

X bits are unknown in function. S is the 'state' of the gas valve/flame. S = 0
means 'flame off'. S = 1 means 'flame on'. T indicates whether or not the remote
is in 'thermo' mode - this is a mode where the remote detects the room temperature
and commands the gas logs on/off to maintain the temperature selected on the remote.

There are safety mechanisms afoot - whenever the gas logs are 'on', on with a timer,
or on in thermo mode, occasional 'keepalive' messages are sent to the gas logs to 
guarantee that the remote is still in range and the batteries are not dead. Generally
these messages are exactly the same as the last command that the remote sent - that is,
if you turn the logs 'on' manually, the remote will send the same 'on' command every so
often. 

The COMMAND S and T bits have these meanings:
    S  T
    ----
    0  0 - Off, Manual mode
    0  1 - Off, Thermo mode (room is too warm)
    1  0 - On,  Manual mode. 
    1  1 - On,  Thermo more (room is too cold)

*/

static int heatilator_fan_decode(r_device *decoder, bitbuffer_t *bitbuffer)
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
            if (decoder->verbose > 1) {
                fprintf(stderr, "%s: Expected %d bits, got %d.\n", __func__, 21, num_bits); // Max number of bits is 21
            }
            continue;
        }

        uint8_t bytes[3]; // Max number of bytes is 3
        bitbuffer_extract_bytes(bitbuffer, row, 1, bytes, 21); // Valid byte offset is 1, Max number of bits is 21
        reflect_bytes(bytes, 3); // Max number of bytes is 3

        // Calculate nibble sum and compare
        int checksum = add_nibbles(bytes, 2) & 0x0f;
        if (checksum != bytes[2]) { // Sum is in byte 2
            if (decoder->verbose > 1) {
                fprintf(stderr, "%s: Checksum failure: expected %0x, got %0x\n", __func__, bytes[2], checksum); // Sum is in byte 2
            }
            continue;
        }

        // Now that message "envelope" has been validated, start parsing data.
        int command = bytes[0] >> 4;    // Command and Channel are in byte 0
        int channel = ~bytes[0] & 0x0f; // Command and Channel are in byte 0
        int value   = bytes[1];         // Value is in byte 1

        char value_string[64] = {0};

        switch (command) {
        case 1: // 1 is the command to STOP
            sprintf(value_string, "stop");
            break;

        case 2: // 2 is the command to change fan speed
            sprintf(value_string, "speed %d", value);
            break;

        case 4: // 4 is the command to change the light intensity
            sprintf(value_string, "%d %%", value);
            break;

        case 5: // 5 is the command to set the light delay
            sprintf(value_string, "%s", value == 0 ? "off" : "on");
            break;

        case 6: // 6 is the command to change fan direction
            sprintf(value_string, "%s", value == 0x07 ? "clockwise" : "counter-clockwise");
            break;

        default:
            if (decoder->verbose > 1) {
                fprintf(stderr, "%s: Unknown command: %d\n", __func__, command);
                continue;
            }
            break;
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",            "",     DATA_STRING,    "Heatilator-Remote",
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

static char *output_fields[] = {
        "model",
        "type",
        "channel",
        "command",
        "value",
        "mic",
        NULL,
};

r_device heatilator_fan = {
        .name        = "Heatilator Ceiling Fan Remote (-f 303.75M to 303.96M)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 580,
        .long_width  = 976,
        .gap_limit   = 8000,
        .reset_limit = 14000,
        .decode_fn   = &heatilator_fan_decode,
        .fields      = output_fields,
};
