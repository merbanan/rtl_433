/** @file
    FSL Cricket Scoreboard Controller.

    Copyright © 2026 David Woodhouse <dwmw2@infradead.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
FSL Cricket Scoreboard Controller.

The device uses FSK PCM encoding with Manchester-encoded data.

Packet structure:

PREAMBLE (38 bits, sent once):
10101010101010101010101010101010101010
PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP

BLOCK (72 bits, repeated 10 times):
111010010110011001101001100101010110101001101010011010101010101010101000
   0 0 1 1 0 1 0 1 0 0 1 0 1 1 1 1 0 0 0 1 0 0 0 1 0 0 0 0 0 0 0 0 0
   <  3  > <  5  > <  2  > <  f  > <  1  > <  1  > <  0  > <  0  >
SSS        F F F F         H H H H         T T T T         U U U U ? ppp

LEGEND:
- P = Preamble (alternating tones for receiver sync)
- S = Sync (111 - 3 bits)
- F = Field nybble (0101 = 5, scoreboard field ID)
- H = Hundreds nybble (1111 = F/blank, with position marker 2)
- T = Tens nybble (0001 = 1, with position marker 1)
- U = Units nybble (0000 = 0, with position marker 0)
- p = Postamble (000 - 3 bits)

Manchester encoding: 01→1, 10→0

Decoding algorithm:
1. Find 111 sync pattern (3 bits)
2. Manchester decode 32 bits starting immediately after sync
3. Extract nybbles directly
4. Nybbles contain: Position(3), Field, Pos(2), Hundreds, Pos(1), Tens, Pos(0), Units

Data format (8 nybbles = 32 bits):
    3 F 2 H 1 T 0 U

Where 3,2,1,0 seem to be digit position markers, and F,H,T,U are the actual data.

There seems to be a 33rd Manchester-encoded bit, which in some cases is 1 for the
first block, and 0 in the nine remaining copies.

TOTAL PACKET: 38 + (72 × 10) = 758 bits

*/
static int fsl_scoreboard_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t b[4];
    int row;
    bitbuffer_t decoded = {0};

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] < 700)
            continue;

        // Search for preamble: 38 bits of alternating 10101010...
        uint8_t const preamble[] = {0xAA, 0xAA, 0xAA, 0xAA};
        unsigned preamble_pos = bitbuffer_search(bitbuffer, row, 0, preamble, 32);

        if (preamble_pos + 38 + 72 >= bitbuffer->bits_per_row[row])
            continue;

        // Blocks start after 38-bit preamble
        unsigned block_pos = preamble_pos + 38;

    // We expect ten blocks, but we only need one good one
        for (block_pos = preamble_pos + 38;
         block_pos + 72 < bitbuffer->bits_per_row[row];
         block_pos += 72) {
            // Verify 111 sync at expected position
            uint8_t const sync[] = {0xE0};
            if (bitbuffer_search(bitbuffer, row, block_pos, sync, 3) != block_pos)
                continue;

            // Manchester data starts after 3-bit sync
            unsigned data_start = block_pos + 3;

            // Decode 32 bits of Manchester
            bitbuffer_clear(&decoded);
            bitbuffer_manchester_decode(bitbuffer, row, data_start, &decoded, 32);

            if (decoded.bits_per_row[0] < 32)
                continue;

            // Extract bytes
            bitbuffer_extract_bytes(&decoded, 0, 0, b, 32);

            int pos3     = b[0] >> 4;
            int field_id = b[0] & 0xF;
            int pos2     = b[1] >> 4;
            int hundreds = b[1] & 0xF;
            int pos1     = b[2] >> 4;
            int tens     = b[2] & 0xF;
            int pos0     = b[3] >> 4;
            int units    = b[3] & 0xF;

            // Validate position markers (3, 2, 1, 0)
            if (pos3 != 0x3 || pos2 != 0x2 || pos1 != 0x1 || pos0 != 0x0)
                continue;

            // Valid block found
            int value = 0;
            if (hundreds != 0xF) value += hundreds * 100;
            if (tens != 0xF) value += tens * 10;
            if (units != 0xF) value += units;

            /* clang-format off */
            data_t *data_out = data_make(
                    "model",    "",             DATA_STRING, "FSL-Scoreboard",
                    "id",       "Field",        DATA_INT,    field_id,
                    "value",    "Value",        DATA_INT,    value,
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data_out);
            return 1;
        }
    }
    return DECODE_ABORT_EARLY;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "value",
        NULL,
};

r_device const fsl_scoreboard = {
        .name        = "FSL Cricket Scoreboard Controller",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 528,
        .long_width  = 528,
        .reset_limit = 3000,
        .decode_fn   = &fsl_scoreboard_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
