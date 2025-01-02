/** @file
    Decoder for Martec ceiling fan remotes

    Copyright (C) 2024 Don Ashdown <>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"
#include <stdbool.h>

/**
Decoder for Martec ceiling fan remotes

Data layout:

    22 bits
    PPPP IIII DDDDDDD SS U CCCC


- P: 4 bit preamble of fixed value 1000
- I: 4 bit channel ID
- D: 7 bit dimmer
- S: 2 bit speed
- U: 1 bit unknown
- C: 4 bit simple checksum

Format string:

    xxxx ID:4h LIGHT:7h FAN:2h x CRC:4b

Dimmer value of 0 is off
Dimmer value between 1 and 41 is on with 1 being full brightness

Treat the data as 4 nibbles commencing with bit 2 to simplify checksum calculation
    P PPPIIIID DDDDDDSS UCCCC

Checksum is simple sum over 4 nibbles

*/

static int martec_fan_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    char const *const speed_names[] = {
            /* 0  */ "off",
            /* 1  */ "high",
            /* 2  */ "medium",
            /* 3  */ "low",
    };

    int return_code = 0;
    uint8_t previous_bytes[3] = {0,1,1}; // Initialize previous bytes to zero
    bool is_first_code = true; // Flag to check if it's the first code

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        int num_bits = bitbuffer->bits_per_row[row];

        if (num_bits != 22) { // Max number of bits is 22
            decoder_logf(decoder, 2, __func__, "Expected %d bits, got %d.", 22, num_bits);
            continue;
        }

        uint8_t bytes[3]; // Max number of bytes is 3
        bitbuffer_extract_bytes(bitbuffer, row, 1, bytes, 21); // Extract 21 bits starting from bit 1
    
        // Skip repeated codes
        if (!is_first_code && memcmp(previous_bytes, bytes, 3) == 0) {
            continue;
        }

        // Calculate nibble sum and compare
        int checksum = add_nibbles(bytes, 2);
        checksum &= 0x0f;
        int cks = (bytes[2] >> 3) & 0x0f;
        if (checksum != cks) { // Sum is in byte 2
            decoder_logf(decoder, 2, __func__, "Checksum failure: expected %x, got %x", cks, checksum);
            continue;
        }

        int channel = (~bytes[0] >> 1) & 0x0f;
        channel = reflect4(channel);
        int dimmer = (bytes[0] & 0x01) << 6;
        // Dimmer ranges from 0 to 41 with 0 being full brightness
        dimmer += (bytes[1] >> 2) & 0x3f;
        //decoder_logf(decoder, 2, __func__, "Pre-dimmer: %02x", dimmer);
        if (dimmer > 0) {
            dimmer = 42 - dimmer;
        }
        int speed = (bytes[1] & 0x03);

        /* clang-format off */
        data_t *data = data_make(
                "model",            "",     DATA_STRING,    "Martec-Remote",
                "id",               "",     DATA_INT,       channel,
                "dimmer",           "",     DATA_INT,       dimmer,
                "speed",            "",     DATA_STRING,    speed_names[speed],
                "mic",              "",     DATA_STRING,    "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return_code++;

        // Update previous_bytes with the current code
        memcpy(previous_bytes, bytes, 3);
        is_first_code = false;
    }

    return return_code;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "dimmer",
        "speed",
        "mic",
        NULL,
};

r_device const martec_fan = {
        .name        = "Martec Ceiling Fan Remote (-f 433.92M)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 292,
        .long_width  = 648,
        .gap_limit   = 850,
        .reset_limit = 12000,
        .decode_fn   = &martec_fan_decode,
        .fields      = output_fields,
};