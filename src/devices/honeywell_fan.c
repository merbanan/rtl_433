/** @file
    Decoder for Honeywell fan remotes.

    Copyright (C) 2020-2022 David E. Tiller

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decoder for Honeywell fan remotes.

This fan is made by Intertek (model 4003229) but is sold by Honeywell
as a 'Harbor Breeze Salermo'.

Honeywell fans use OOK_PULSE_PPM encoding.
The packet starts with 576 uS start pulse.
- 0 is defined as a 300 uS gap followed by a 900 uS pulse.
- 1 is defined as a 900 uS gap followed by a 300 uS pulse.

Transmissions consist of a short start bit followed by bursts of 24 bits.
These packets are repeated up to 23 times.

Possible packet layout:

    Bit number 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23
               -----------------------------------------------------------------------
    Value      0  0  0  1  0  1  1  0  1  1  0  0  1  1  0  1 |Value|  Cmd   | 1 !d  d

It is pure supposition that the leading 0x16CD and bit 21 are fixed values.
I do not have more than 1 remote to test and there's no mention in the manual about
dip switch settings, nor are there any on the remote. It's also possible that the
value occupies 3 bits and the command is only two bits. It's also possible that
there's no such command/value distinction. It looks very suspicious that the fan
speed commands all share command 000 and the speed value (bit-reversed) appears in the
value area.

Button  Fixed Other Bits       Function
ONE     16CD  1 0 0 0 0 1 !d d  Low speed fan
TWO     16CD  0 1 0 0 0 1 !d d  Medium speed fan
THREE   16CD  1 1 0 0 0 1 !d d  High speed fan
OFF-M   16CD  0 0 0 1 0 1 !d d  Fan off (momentary press)
OFF-C   16CD  0 0 1 0 1 1 !d d  Light off delay (continuous press)
STAR-M  16CD  1 1 0 1 0 1 !d d  Light on/off (momentary press)
STAR-C  16CD  0 1 1 1 0 1 !d d  Light dim/brighten (continuous press)

The 'd' bit indicates whether the D/CFL button in the battery compartment
is set to 'D' (1 bit) or 'CFL' (0 bit). This switch inhibits the dim
function when set to CFL. The !d bit seems to just be the complement of 'd'.

Since the COMMAND/VALUE paradigm is not verified and only seems to apply to the fan speed
buttons, we'll decode using the full 3rd byte right-shifted by 3 bits to omit the fixed '1'
and 'Dim' bits.

byte[2] >> 3:
    0x10: Low fan speed
    0x08: Medium fan speed
    0x18: High fan speed
    0x02: Fan off, momentary press of the power button
    0x05: Delayed light off, extended press of the power button
    0x1A: Light on/off, momentary press of the 'star' button
    0x0E: Light dim/brighten, extended press of the 'star' button

*/

static int honeywell_fan_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int return_code = 0;

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        int num_bits = bitbuffer->bits_per_row[row];

        if (num_bits != 24) { // Correct number of bits is 24
            if (decoder->verbose > 1) {
                fprintf(stderr, "%s: Expected %d bits, got %d.\n", __func__, 21, num_bits); // Max number of bits is 21
            }
            continue;
        }

        uint8_t bytes[3] = {0}; // Max number of bytes is 3
        bitbuffer_extract_bytes(bitbuffer, row, 0, bytes, 24); // Valid byte offset is 0, Max number of bits is 24

        // Sanity check leading 'fixed' portion
        if (bytes[0] != 0x16 || bytes[1] != 0xcd) {
            if (decoder->verbose > 1) {
                fprintf(stderr, "%s: Expected leading fixed bits 0x16CD, got %x%x.\n", __func__, bytes[0], bytes[1]);
            }
            continue;
        }

        int dimmable = bytes[2] & 0x01;
        int command = (bytes[2] >> 3) & 0x1f;
        char command_string[80] = {0};

        switch (command) {
        case 0x10: // Low fan speed
            sprintf(command_string, "fan_low");
            break;

        case 0x08: // Medium fan speed
            sprintf(command_string, "fan_medium");
            break;

        case 0x18: // Hign fan speed
            sprintf(command_string, "fan_high");
            break;

        case 0x02: // Fan off
            sprintf(command_string, "fan_off");
            break;

        case 0x05: // Delayed light off
            sprintf(command_string, "light_off_delayed");
            break;

        case 0x1a: // Light on/off
            sprintf(command_string, "light_on_off");
            break;

        case 0x0e: // Light dim/brighten
            sprintf(command_string, "light_dim_brighten");
            break;

        default:
            if (decoder->verbose > 1) {
                fprintf(stderr, "%s: Unknown command: %d\n", __func__, command);
                continue;
            }
            break;
        }

        // clang-format off
        data_t *data = data_make(
                "model",            "",     DATA_STRING,    "Honeywell-Remote",
                "command",          "",     DATA_STRING,    command_string,
                "dimmable",         "",     DATA_INT   ,    dimmable,
                "mic",              "",     DATA_STRING,    "FIXED_BITS",
                NULL);
        // clang-format on

        decoder_output_data(decoder, data);
        return_code++;
    }

    return return_code;
}

static char *output_fields[] = {
        "model",
        "command",
        "dimmable",
        "mic",
        NULL,
};
// OOK_PULSE_PPM,s=300,l=900,r=1300 works to get one row
r_device honeywell_fan = {
        .name        = "Honeywell Ceiling Fan Remote (-f 303.75M to 303.96M)",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 300,
        .long_width  = 900,
        //.gap_limit   = 2200,
        .reset_limit = 1300,
        .decode_fn   = &honeywell_fan_decode,
        .fields      = output_fields,
};
