/** @file
    Solight TE44 temperature sensor.

    Copyright (C) 2017 Miroslav Oujesky

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Solight TE44 -- Generic wireless thermometer, which might be sold as part of different kits.

Note: this is identical with Rubicson and is only active when Rubicson is disabled.

So far these were identified (mostly sold in central/eastern europe)
- Solight TE44
- Solight TE66
- EMOS E0107T
- NX-6876-917 from Pearl (for FWS-70 station).
- newer TFA 30.3197

Rated -50 C to 70 C, frequency 433,92 MHz, three selectable channels.

Data structure:

12 repetitions of the same 36 bit payload, 1bit zero as a separator between each repetition.

    36 bit payload format: iiiiiiii b0cctttt tttttttt 1111xxxx xxxx

- i: 8 bit random key (changes after device reset)
- b 1 bit battery flag: 1 if battery is ok, 0 if battery is low
- c: 2 bit channel (0-2)
- t: 12 bit temperature in celsius, signed integer, scale 10
- x: 8 bit checksum

*/
static int solight_te44_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 36);
    if (r < 0) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *b = bitbuffer->bb[r];

    if (bitbuffer->bits_per_row[r] != 37) {
        return DECODE_ABORT_LENGTH;
    }

    if ((b[3] & 0xf0) != 0xf0) {
        return DECODE_ABORT_EARLY; // const not 1111
    }

    // CRC check
    uint8_t tmp[5];
    tmp[0] = b[0];                // Byte 0 is nibble 0 and 1
    tmp[1] = b[1];                // Byte 1 is nibble 2 and 3
    tmp[2] = b[2];                // Byte 2 is nibble 4 and 5
    tmp[3] = b[3] & 0xf0;         // Byte 3 is nibble 6 and 0-padding
    tmp[4] = (b[3] & 0x0f) << 4 | // CRC is nibble 7 and 8
             (b[4] & 0xf0) >> 4;

    int chk = crc8(tmp, 5, 0x31, 0x6c);
    if (chk) {
        return DECODE_FAIL_MIC;
    }

    int id       = b[0];
    // int battery  = (b[1] & 0x80);
    int channel  = ((b[1] & 0x30) >> 4);
    int temp_raw = (int16_t)((b[1] << 12) | (b[2] << 4)); // sign-extend
    float temp_c = (temp_raw >> 4) * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Solight-TE44",
            "id",               "Id",           DATA_INT,    id,
            "channel",          "Channel",      DATA_INT,    channel + 1,
//            "battery_ok",       "Battery",      DATA_INT,    !!battery,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        //"battery_ok",
        "temperature_C",
        "mic",
        NULL,
};

r_device const solight_te44 = {
        .name        = "Solight TE44/TE66, EMOS E0107T, NX-6876-917",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 972,  // short gap = 972 us
        .long_width  = 1932, // long gap = 1932 us
        .gap_limit   = 3000, // packet gap = 3880 us
        .reset_limit = 6000,
        .decode_fn   = &solight_te44_decode,
        .priority    = 10, // Eliminate false positives by letting Rubicson-Temperature go earlier
        .fields      = output_fields,
};
