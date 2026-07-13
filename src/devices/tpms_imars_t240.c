/** @file
    iMars T240 TPMS.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
iMars T240 TPMS.

Sold e.g. as "IMars T240 TPMS Solar Power Tire Pressure Monitor System"
on Banggood, using an SP372 sensor IC and an atA5754 receiver IC.

https://github.com/merbanan/rtl_433/issues/1820

- Frequency: 433.92 MHz
- Modulation: OOK, Manchester encoded
- Half-bit time: ~50 us

Packet structure: 32-bit raw alternating preamble (0xaaaaaaaa), then
128 raw bits Manchester decoding to 8 bytes (B0..B7).

Decoded byte layout:

    B0 B1 B2 B3 B4 B5 B6 B7

- B7 repeats B0 (tail integrity check)
- (B0 & 0x0f) == (B1 & 0x0f)
- B3 and B4 are each an affine function of B0 (B3 = B0 - k1, B4 = k2 - B0),
  so (B3 + B4) mod 256 == (k2 - k1) mod 256 is a fixed per-unit checksum

Two physical sensor units are confirmed in the issue's captures, each with
its own (k1, k2) and thus its own checksum constant: 0x41 and 0x3c. Only
those two are known; a third unit would need its own constant added here.

The mapping of B2/B5/B6 to temperature and pressure has not been
determined: no capture in the issue thread has a corresponding ground
truth reading (device display) to calibrate scale/offset against.
*/

static int tpms_imars_t240_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[3] = {0xaa, 0xaa, 0xaa};

    if (bitbuffer->num_rows != 1) {
        decoder_logf(decoder, 2, __func__, "Row error");
        return DECODE_ABORT_EARLY;
    }

    int len = bitbuffer->bits_per_row[0];
    int pos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 24);
    if (pos >= len) {
        decoder_logf(decoder, 2, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }

    if (len - pos < 160) {
        decoder_logf(decoder, 2, __func__, "Too short");
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_t packet_bits = {0};
    bitbuffer_manchester_decode(bitbuffer, 0, pos + 32, &packet_bits, 64);
    bitbuffer_invert(&packet_bits);

    if (packet_bits.bits_per_row[0] < 64) {
        return DECODE_FAIL_SANITY;
    }
    uint8_t *b = packet_bits.bb[0];

    if (b[7] != b[0])
        return DECODE_FAIL_SANITY;
    if ((b[0] & 0x0f) != (b[1] & 0x0f))
        return DECODE_FAIL_SANITY;
    int checksum = (b[3] + b[4]) & 0xff;
    if (checksum != 0x41 && checksum != 0x3c) {
        decoder_logf(decoder, 2, __func__, "Checksum error");
        return DECODE_FAIL_MIC;
    }

    char code_str[7 * 2 + 1];
    snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x%02x%02x",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6]);

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",             DATA_STRING, "iMars-T240",
            "type",     "",             DATA_STRING, "TPMS",
            "code",     "",             DATA_STRING, code_str,
            "mic",      "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "code",
        "mic",
        NULL,
};

r_device const tpms_imars_t240 = {
        .name        = "iMars T240 TPMS",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 50,
        .long_width  = 50,
        .reset_limit = 200,
        .decode_fn   = &tpms_imars_t240_decode,
        .fields      = output_fields,
};
