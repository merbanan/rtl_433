/** @file
    2GIG-KEY2E-345 encrypted 4-button keyfob.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
2GIG-KEY2E-345 encrypted 4-button keyfob.

Same OOK/Manchester-zerobit family and 24-bit raw preamble (0x555556) as
the plain Honeywell/2Gig door-window sensors in honeywell.c, but a longer,
72-bit (post-preamble) frame:

    IIIIIIII IIIIIIII IIIIIIII IIIIIIII 00100101 SSSSSSSS SSSSSSSS CCCCCCCC CCCCCCCC

- I: 32 bit, believed encrypted (device id and/or rolling counter)
- 8 bit constant, 0x25 in every sample seen so far
- S: 16 bit, believed encrypted (button/status)
- C: 16 bit CRC, poly 0x8005, init 0x4c57

Reverse-engineered in issue #2584 (zuckschwerdt, klohner, dfiore1230):
CRC confirmed against 7 real codes from two different physical units.
The 32+16 encrypted bits are unsolved -- 2GIG's "eSeries Encrypted
Technology" is undocumented, and no two samples exist of the same
button on the same unit pressed twice, so there isn't enough data to
even tell whether it's a rolling code. Reported here as opaque hex,
not as an "id" -- ships disabled until the payload can be decoded.
*/

static int twogig_key2e_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[3] = {0x55, 0x55, 0x56};

    int row = 0;
    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[row] < 96) {
        return DECODE_ABORT_LENGTH;
    }

    int raw_len = bitbuffer->bits_per_row[row];
    uint8_t b[9] = {0};

    unsigned raw_pos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, 24);
    if (raw_pos + 24 >= (unsigned)raw_len) {
        return DECODE_ABORT_EARLY;
    }

    bitbuffer_t decoded = {0};
    bitbuffer_manchester_decode(bitbuffer, row, raw_pos + 24, &decoded, 72);
    if (decoded.bits_per_row[0] < 72) {
        return DECODE_ABORT_LENGTH;
    }
    memcpy(b, decoded.bb[0], 9);

    if (b[4] != 0x25) {
        return DECODE_ABORT_EARLY;
    }
    uint16_t crc_calc = crc16(b, 7, 0x8005, 0x4c57);
    uint16_t crc_recv = (b[7] << 8) | b[8];
    if (crc_calc != crc_recv) {
        return DECODE_FAIL_MIC;
    }

    char enc_id[9];
    char enc_status[5];
    snprintf(enc_id, sizeof(enc_id), "%02x%02x%02x%02x", b[0], b[1], b[2], b[3]);
    snprintf(enc_status, sizeof(enc_status), "%02x%02x", b[5], b[6]);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "TwoGig-KEY2E345",
            "encrypted_id",     "Encrypted ID", DATA_STRING, enc_id,
            "encrypted_status", "Encrypted Status", DATA_STRING, enc_status,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "encrypted_id",
        "encrypted_status",
        "mic",
        NULL,
};

r_device const twogig_key2e = {
        .name        = "2GIG-KEY2E-345 encrypted keyfob",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 136,
        .long_width  = 136,
        .reset_limit = 408,
        .decode_fn   = &twogig_key2e_decode,
        .fields      = output_fields,
        .disabled    = 1, // payload undecoded, see issue #2584
};
