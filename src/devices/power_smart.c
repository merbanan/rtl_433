/** @file
    Power Smart blind remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Power Smart blind remotes.

Seen on 433.92 MHz. Manchester coded with 225 us half bits, a 1 bit is high then low.
A message is 64 bits, sent 8 times without a gap.

Data layout:

    FD KCCCCCCD DDDDDDDK YY AA ~(KCCCCCCD DDDDDDDK) WW

- FD, AA: sync bytes
- K: 2 bit button, first and last bit of the 16 bits: 1 down, 2 up, 3 stop
- C: 6 bit channel
- D: 8 bit data
- Y: 8 bit data
- the 16 bits K C D K are repeated inverted
- W: 8 bit check, Y = ~W - 1

The id is the C D Y bits, as on the Flipper Zero.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/power_smart.c`.

Flex decoder:

    rtl_433 -X 'n=power_smart,m=OOK_PCM,s=225,l=225,r=1000,preamble=aaa6'
*/

static int power_smart_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // 0xfd Manchester coded, 1 = 10
    uint8_t const preamble[] = {0xaa, 0xa6};

    int ret = DECODE_ABORT_EARLY;
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned len = bitbuffer->bits_per_row[row];
        unsigned pos = 0;
        while ((pos = bitbuffer_search(bitbuffer, row, pos, preamble, 16)) + 128 <= len) {
            bitbuffer_t mc = {0};
            bitbuffer_manchester_decode(bitbuffer, row, pos, &mc, 64);
            pos += 2;
            if (mc.bits_per_row[0] < 64) {
                ret = DECODE_ABORT_LENGTH;
                continue;
            }
            bitbuffer_invert(&mc);
            uint8_t *b = mc.bb[0];

            if (b[0] != 0xfd || b[4] != 0xaa) {
                ret = DECODE_FAIL_SANITY;
                continue;
            }
            // Validate inverted copy and check byte
            if ((b[1] ^ b[5]) != 0xff || (b[2] ^ b[6]) != 0xff || b[3] != (uint8_t)(~b[7] - 1)) {
                ret = DECODE_FAIL_MIC;
                continue;
            }

            int button  = ((b[1] >> 6) & 0x2) | (b[2] & 0x1);
            int channel = (b[1] >> 1) & 0x3f;
            int id      = ((((b[1] & 0x7f) << 7) | (b[2] >> 1)) << 8) | b[3];

            char const *const control[] = {"Unknown", "Down", "Up", "Stop"};

            /* clang-format off */
            data_t *data = data_make(
                    "model",    "",         DATA_STRING, "PowerSmart-Remote",
                    "id",       "",         DATA_FORMAT, "%05x", DATA_INT, id,
                    "channel",  "Channel",  DATA_INT,    channel,
                    "button",   "Button",   DATA_INT,    button,
                    "control",  "Control",  DATA_STRING, control[button],
                    "mic",      "Integrity", DATA_STRING, "CHECKSUM",
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            return 1;
        }
    }
    return ret;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "button",
        "control",
        "mic",
        NULL,
};

r_device const power_smart = {
        .name        = "Power Smart blind remotes",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 225,
        .long_width  = 225,
        .reset_limit = 1000,
        .decode_fn   = &power_smart_decode,
        .fields      = output_fields,
};
