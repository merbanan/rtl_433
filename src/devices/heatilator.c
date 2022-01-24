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

static int heatilator_log_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    char const * const mode_names[] = {
            /* 0  */ "manual",
            /* 1  */ "thermo",
    };

    char const * const state_names[] = {
            /* 0  */ "flame_off",
            /* 1  */ "flame_on",
    };

    int return_code = 0;

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        int num_bits = bitbuffer->bits_per_row[row];

        if (num_bits < 24 || num_bits > 25) {
            if (decoder->verbose > 1) {
                fprintf(stderr, "%s: Expected 24 or 25 bits, got %d.\n", __func__, num_bits);
            }
            continue;
        }

        uint8_t bytes[3]; // Max number of bytes is 3
        bitbuffer_extract_bytes(bitbuffer, row, 0, bytes, 24); // Valid byte offset is 0, Max number of bits is 24

        int serial_number = bytes[0] << 12 | bytes[1] << 4 | bytes[2] >> 4;
        int command = bytes[2] & 0x0f;
        int mode = command & 0x01;
        int state = command >> 1 & 0x01;

        /* clang-format off */
        data_t *data = data_make(
                "model",            "",     DATA_STRING,    "Heatilator-Remote",
                "serial_number",    "",     DATA_INT,       serial_number,
                "mode",             "",     DATA_STRING,    mode_names[mode],
                "state",            "",     DATA_STRING,    state_names[state],
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return_code++;
    }

    return return_code;
}

static char *output_fields[] = {
        "model",
        "serial_number",
        "mode",
        "state",
        NULL,
};

r_device heatilator_log = {
        .name        = "Heatilator Gas Log Remote",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 380,
        .long_width  = 1420,
        .reset_limit = 1800,
        .decode_fn   = &heatilator_log_decode,
        .fields      = output_fields,
        .disabled    = 1,
};
