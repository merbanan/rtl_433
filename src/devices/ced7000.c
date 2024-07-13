/** @file
    CED7000 Shot Timer

    Copyright (C) 2023 Pierros Papadeas <pierros@papadeas.gr>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
CED7000 Shot Timer, also CED8000.

FSK_PCM with 1300 us short, 1300 us long, and 3500 us gap.
Sync is a 0xaa4d5e, then payload.
The data is repeated 3 times.

Data layout:

    II II CC FF FF FS SS SS UU UU U

- I: RFID, 16 bit LSB, reversed in order, decimal representation per 4 bits, 4 digits
- C: shot counter, 8 bit LSB, reversed in order, decimal representation per 4 bits, 2 digits
- F: final time, 20 bit LSB, reversed in order, decimal representation per 4 bits, 5 digits with 2 decimal points assumed
- S: split time, 20 bit LSB, reversed in order, decimal representation per 4 bits, 5 digits with 2 decimal points assumed
- U: unknown 20 bits, possible checksum and ending sync word

*/

#include "decoder.h"

#define NUM_BITS_PREAMBLE (32)
#define NUM_BITS_DATA (169)
#define NUM_BITS_TOTAL (201)

static int ced7000_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_t decoded = { 0 };
    int ret = 0;
    int bitpos = 0;
    uint8_t *b;

    /* Find row repeated at least twice */
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 6*16+3*8);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    /* Search for 24 bit sync pattern */
    uint8_t const sync_pattern[3] = {0xaa, 0x4d, 0x5e};
    bitpos = bitbuffer_search(bitbuffer, row, bitpos, sync_pattern, 24) + 24;

    if (bitpos >= bitbuffer->bits_per_row[row]) {
        return DECODE_ABORT_EARLY;
    }

    bitbuffer_invert(bitbuffer);

    /* Check and decode the Manchester bits */
    ret = bitbuffer_manchester_decode(bitbuffer, row, bitpos, &decoded, NUM_BITS_DATA);
    if (ret != NUM_BITS_TOTAL + 1) {
        decoder_log(decoder, 2, __func__, "invalid Manchester data");
        return DECODE_FAIL_MIC;
    }

    /* Get the decoded data fields */
    /* IIIIIIII IIIIIIII CCCCCCCC FFFFFFFF FFFFFFFF FFFFSSSS
       SSSSSSSS SSSSSSSS UUUUUUUU UUUUUUUU UUUUxxxx*/

    b = decoded.bb[0];

    /* Reverse the bit order per nibble */
    reflect_nibbles(b, ret / 8);

    /* Read the values */
    int id = (b[1] & 0xF) * 1000 + (b[1] >> 4) * 100 + (b[0] & 0xF) * 10 + (b[0] >> 4);
    int count = (b[2] & 0xF) * 10 + (b[2] >> 4);
    float final = (b[5] >> 4) * 100 + (b[4] & 0xF) * 10 + (b[4] >> 4) + (b[3] & 0xF) * 0.1 + (b[3] >> 4) * 0.01f;
    float split = (b[7] & 0xF) * 100 + (b[7] >> 4) * 10 + (b[6] & 0xF) + (b[6] >> 4) * 0.1 + (b[5] & 0xF) * 0.01f;

    /* clang-format off */
    data_t *data = data_make(
            "model",    "Model",       DATA_STRING, "CED7000",
            "id",       "ID",          DATA_FORMAT, "%04u",    DATA_INT, id,
            "count",    "Shot Count",  DATA_INT,    count,
            "final",    "Final Time",  DATA_FORMAT, "%.2f s", DATA_DOUBLE, final,
            "split",    "Split Time",  DATA_FORMAT, "%.2f s", DATA_DOUBLE, split,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "count",
        "final",
        "split",
        NULL,
};

r_device const ced7000 = {
        .name        = "CED7000 Shot Timer",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 1300,
        .long_width  = 1300,
        .gap_limit   = 3500,
        .reset_limit = 9000,
        .decode_fn   = &ced7000_decode,
        .disabled    = 1, // no fix id, no checksum
        .fields      = output_fields,
};
