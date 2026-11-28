/** @file
    Baylan water meter with UM-1 RF module.

    Copyright (C) 2026 Jarkko Lehti <grue@iki.fi>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Baylan water meter with UM-1 RF module.

The module transmits every 15 seconds on about 867.96 MHz, 2-FSK with
roughly +/-20 kHz deviation, 38.4 kbps NRZ (CC110x style framing).

Use `-f 868M -s 1M`.

Frame layout after the preamble and sync word:

    AAAAAAAA D391 D391 II II II II VV VV VV VV CC CC

- A 32b: preamble 0xaa...
- S 32b: 2 x 16-bit sync word d391 d391 (CC110x default)
- I 32b: meter ID, BCD, least significant byte first
- V 32b: volume in 0.1 m3, BCD, least significant byte first
- C 16b: CRC-16, poly 0x8005, init 0xffff, over the 8 data bytes
         (CC110x hardware CRC, CRC-16/CMS)

Example: `47 16 08 00 91 62 00 00 f7 a2` -> ID 00081647, volume 629.1 m3.

The long zero runs in the payload can make the bit slicer drop or insert
a bit (seen as `... 00 01 ef44` and `... 00 00 7bd1`); the CRC rejects these.
*/

#include "decoder.h"

static int bcd_le_valid(uint8_t const *b, int n)
{
    for (int i = 0; i < n; ++i) {
        if ((b[i] & 0x0f) > 9 || (b[i] >> 4) > 9) {
            return 0;
        }
    }
    return 1;
}

static uint32_t bcd_le_decode(uint8_t const *b, int n)
{
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; --i) {
        v = v * 100 + (b[i] >> 4) * 10 + (b[i] & 0x0f);
    }
    return v;
}

static int baylan_um1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xaa, 0xd3, 0x91, 0xd3, 0x91};

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    unsigned pos = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8);
    if (pos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }
    pos += sizeof(preamble) * 8;

    if (pos + 10 * 8 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[10];
    bitbuffer_extract_bytes(bitbuffer, 0, pos, b, 10 * 8);

    decoder_log_bitrow(decoder, 1, __func__, b, 10 * 8, "frame");

    uint16_t crc = crc16(b, 8, 0x8005, 0xffff);
    if (crc != (b[8] << 8 | b[9])) {
        decoder_logf(decoder, 1, __func__, "CRC invalid %04x != %02x%02x", crc, b[8], b[9]);
        return DECODE_FAIL_MIC;
    }

    if (!bcd_le_valid(b, 8)) {
        decoder_log(decoder, 1, __func__, "invalid BCD digits");
        return DECODE_FAIL_SANITY;
    }

    uint32_t id     = bcd_le_decode(&b[0], 4);
    uint32_t volume = bcd_le_decode(&b[4], 4);

    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%08u", (unsigned)id);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "Baylan-UM1",
            "id",           "Meter ID",     DATA_STRING, id_str,
            "volume_m3",    "Volume",       DATA_FORMAT, "%.1f m3", DATA_DOUBLE, volume * 0.1,
            "mic",          "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "volume_m3",
        "mic",
        NULL,
};

r_device const baylan_um1 = {
        .name        = "Baylan water meter with UM-1 RF module (-f 868M -s 1M)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 26,
        .long_width  = 26,
        .reset_limit = 1000,
        .decode_fn   = &baylan_um1_decode,
        .fields      = output_fields,
};
