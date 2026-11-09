/** @file
    GangQi alarm system remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
GangQi alarm system remotes.

Seen on 433.92 MHz. PWM coded, a 1 bit is a 1200 us pulse and a 500 us gap, a 0 bit is a
500 us pulse and a 1200 us gap. A message is 34 bits, repeated after a 2.2 ms gap.

Data layout:

    SSSS C B XX 2

- S: 16 bit serial
- C: 4 bit constant 0xd
- B: 4 bit button, e.g. 0xd arm, 0xe disarm, 0xb alarm, 0x7 ring
- X: 8 bit checksum, 0xc8 minus the bytes S, S, CB (the remotes),
     or 0x02 plus the bytes (also accepted by the receivers)
- 2: 2 bit, 0

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/gangqi.c`.

Flex decoder:

    rtl_433 -X 'n=gangqi,m=OOK_PWM,s=500,l=1200,g=2000,r=5000,t=300,invert,bits=34'
*/

static int gangqi_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    int ret = DECODE_ABORT_LENGTH;
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] != 34) {
            continue;
        }
        uint8_t *b = bitbuffer->bb[row];

        // Constant nibble and trailing zero bits
        if ((b[2] >> 4) != 0xd || (b[4] & 0xc0)) {
            ret = DECODE_FAIL_SANITY;
            continue;
        }

        // Validate checksum
        // 0xc8 minus the bytes (the remotes) or 0x02 plus the bytes (also accepted by the receivers)
        uint8_t sum = b[3];
        uint8_t add = b[0] + b[1] + b[2];
        if (sum != (uint8_t)(0xc8 - add) && sum != (uint8_t)(0x02 + add)) {
            ret = DECODE_FAIL_MIC;
            continue;
        }

        int id     = (b[0] << 8) | b[1];
        int button = b[2] & 0x0f;

        /* clang-format off */
        data_t *data = data_make(
                "model",    "",             DATA_STRING, "GangQi-Remote",
                "id",       "",             DATA_FORMAT, "%04x", DATA_INT, id,
                "button",   "Button",       DATA_FORMAT, "%x", DATA_INT, button,
                "mic",      "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return ret;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        "mic",
        NULL,
};

r_device const gangqi = {
        .name        = "GangQi alarm system remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 500,
        .long_width  = 1200,
        .gap_limit   = 2000,
        .reset_limit = 5000,
        .tolerance   = 300,
        .decode_fn   = &gangqi_decode,
        .fields      = output_fields,
};
