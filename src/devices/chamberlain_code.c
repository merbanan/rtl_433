/** @file
    Chamberlain / LiftMaster DIP switch garage door remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Chamberlain / LiftMaster DIP switch (fixed code) garage door remotes, 7, 8 or 9 switches.

Seen on 300 to 390 MHz. PCM coded with 1 ms slots, a message starts after a 36 ms gap.
Each bit is a symbol of 4 slots:

- 0 bit: 0111 (1 ms low, 3 ms high)
- 1 bit: 0011 (2 ms low, 2 ms high)
- stop: 0001 (3 ms low, 1 ms high)

Message layout (S stop, D bit, first bit sent first):

- 9 bit: S DDDDDDDDD S
- 8 bit: S DDDDD S DD S (the stop takes the place of bit 2)
- 7 bit: S DDDDDD S S D S

The first 3 low slots of the first stop are part of the gap, so a row starts with its high slot.

The dip switch string has the order of the Flipper Zero display: 9 bit 8 7 6 5 4 3 0 1 2,
8 bit 7 6 5 4 3 x 0 1, 7 bit 6 5 4 3 2 1 0. In the 8 bit format the `x` is the position of
the stop that takes the place of bit 2, there is no switch for it.

There is no integrity check, apart from the symbol and stop positions.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/chamberlain_code.c`.

Flex decoder:

    rtl_433 -X 'n=chamberlain_code,m=OOK_PCM,s=1000,l=1000,r=5000'
*/

#define CHAMB_SYM_0    0x7
#define CHAMB_SYM_1    0x3
#define CHAMB_SYM_STOP 0x1

static int chamberlain_code_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = DECODE_ABORT_LENGTH;
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        // Up to the last high slot, the gap after it is added as 0 slots
        unsigned len = bitbuffer->bits_per_row[row];
        while (len > 0 && !bitrow_get_bit(bitbuffer->bb[row], len - 1)) {
            len--;
        }
        // high slot of the first stop, then 9 or 10 symbols
        if (len != 1 + 36 && len != 1 + 40) {
            continue;
        }
        uint8_t *bb = bitbuffer->bb[row];
        if (!(bb[0] & 0x80)) {
            ret = DECODE_ABORT_EARLY;
            continue;
        }

        int count = (len - 1) / 4;
        uint8_t sym[10] = {0};
        for (int i = 0; i < count; ++i) {
            uint8_t s = 0;
            bitbuffer_extract_bytes(bitbuffer, row, 1 + 4 * i, &s, 4);
            sym[i] = s >> 4;
        }

        // Stop positions per length
        int bits;
        if (count == 9 && sym[5] == CHAMB_SYM_STOP && sym[8] == CHAMB_SYM_STOP) {
            bits = 8;
        }
        else if (count == 10 && sym[6] == CHAMB_SYM_STOP && sym[7] == CHAMB_SYM_STOP && sym[9] == CHAMB_SYM_STOP) {
            bits = 7;
        }
        else if (count == 10 && sym[9] == CHAMB_SYM_STOP) {
            bits = 9;
        }
        else {
            ret = DECODE_FAIL_SANITY;
            continue;
        }

        // Bits in send order, the stops skipped
        int code  = 0;
        int nbits = 0;
        int valid = 1;
        for (int i = 0; i < count - 1; ++i) {
            if ((bits == 8 && i == 5) || (bits == 7 && (i == 6 || i == 7))) {
                continue;
            }
            if (sym[i] == CHAMB_SYM_1) {
                code = (code << 1) | 1;
            }
            else if (sym[i] == CHAMB_SYM_0) {
                code = code << 1;
            }
            else {
                valid = 0;
                break;
            }
            nbits++;
            // The 8 bit stop takes the place of bit 2: after the first 5 bits (bits 7 to 3)
            // shift in a 0 placeholder for bit 2, the last 2 bits are then bits 1 and 0
            if (bits == 8 && nbits == 5) {
                code <<= 1;
            }
        }
        if (!valid) {
            ret = DECODE_FAIL_SANITY;
            continue;
        }

        char dip[10];
        if (bits == 9) {
            int const order[9] = {8, 7, 6, 5, 4, 3, 0, 1, 2};
            for (int i = 0; i < 9; ++i) {
                dip[i] = (code >> order[i]) & 1 ? '1' : '0';
            }
            dip[9] = '\0';
        }
        else if (bits == 8) {
            int const order[8] = {7, 6, 5, 4, 3, -1, 0, 1};
            for (int i = 0; i < 8; ++i) {
                dip[i] = order[i] < 0 ? 'x' : (code >> order[i]) & 1 ? '1'
                                                                     : '0';
            }
            dip[8] = '\0';
        }
        else {
            for (int i = 0; i < 7; ++i) {
                dip[i] = (code >> (6 - i)) & 1 ? '1' : '0';
            }
            dip[7] = '\0';
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",    "",             DATA_STRING, "Chamberlain-Code",
                "bits",     "Bits",         DATA_INT,    bits,
                "id",       "",             DATA_FORMAT, "%03x", DATA_INT, code,
                "dip",      "Dip switches", DATA_STRING, dip,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return ret;
}

static char const *const output_fields[] = {
        "model",
        "bits",
        "id",
        "dip",
        NULL,
};

r_device const chamberlain_code = {
        .name        = "Chamberlain / LiftMaster DIP switch remotes",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 1000,
        .long_width  = 1000,
        .reset_limit = 5000, // above the 3 ms low slots, below the 36 ms message gap
        .decode_fn   = &chamberlain_code_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields,
};
