/** @file
    Decoder for Martec MPLCD ceiling fan remotes

    Copyright (C) 2024 Don Ashdown

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decoder for Martec MPLCD ceiling fan remotes

Remote keeps knowledge of fan state and sends combined light and fan setting on each button press.

Data layout:

    22 bits
    PPPP IIII DDDDDDD SS U CCCC

- P: 4 bit fixed preamble 0x8
- I: 4 bit channel ID - reflected and inverted
- D: 7 bit dimmer - 0 is off, 1-41 is on with 1 being full brightness
- S: 2 bit speed - 0: off, 1: high, 2: medium, 3: low
- U: 1 bit unknown
- C: 4 bit simple checksum

Format string:

    xxxx ID:4h LIGHT:7h FAN:2h x CRC:4b

Process the data as 3 bytes skipping the first bit to simplify checksum calculation
    P PPPIIIID DDDDDDSS UCCCC

Checksum is simple sum over 4 nibbles starting from bit 2

See https://github.com/merbanan/rtl_433/pull/3133
*/

static int martec_mplcd_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    char const *const speed_names[] = {
            /* 0  */ "off",
            /* 1  */ "high",
            /* 2  */ "medium",
            /* 3  */ "low",
    };

    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 22);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[row] > 22) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t bytes[3]; // Max number of bytes is 3
    bitbuffer_extract_bytes(bitbuffer, row, 1, bytes, 21); // Extract 21 bits starting from bit 1

    // Calculate nibble sum and compare
    int checksum = add_nibbles(bytes, 2);
    checksum &= 0x0f;
    int cks = (bytes[2] >> 3) & 0x0f;
    if (checksum != cks) { // Sum is in byte 2
        decoder_logf(decoder, 2, __func__, "Checksum failure: expected %x, got %x", cks, checksum);
        return DECODE_FAIL_MIC;
    }

    int channel = (~bytes[0] >> 1) & 0x0f;
    channel = reflect4(channel);
    int dimmer = (bytes[0] & 0x01) << 6;
    // Dimmer ranges from 1 to 41 with 1 being full brightness
    dimmer += (bytes[1] >> 2) & 0x3f;
    // Map dimmer to a continuous range from 0 to 41 with 0 being off and 41 being full brightness
    if (dimmer > 0) {
        dimmer = 42 - dimmer;
    }
    int speed = (bytes[1] & 0x03);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",     DATA_STRING,    "Martec-MPLCD",
            "id",               "",     DATA_INT,       channel,
            "dimmer",           "",     DATA_INT,       dimmer,
            "speed",            "",     DATA_STRING,    speed_names[speed],
            "mic",              "",     DATA_STRING,    "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "dimmer",
        "speed",
        "mic",
        NULL,
};

r_device const martec_mplcd = {
        .name        = "Martec MPLCD Ceiling Fan Remote",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 292,
        .long_width  = 648,
        .gap_limit   = 850,
        .reset_limit = 12000,
        .decode_fn   = &martec_mplcd_decode,
        .fields      = output_fields,
};
