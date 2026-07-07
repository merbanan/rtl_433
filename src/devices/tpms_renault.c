/** @file
    FSK 9 byte Manchester encoded TPMS with CRC.

    Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
FSK 9 byte Manchester encoded TPMS with CRC.
Seen on Renault Clio, Renault Captur, Renault Zoe and maybe Dacia Sandero.

Packet nibbles:

    F F/P PP TT II II II ?? ?? CC

- F = flags, (seen: c0: 22% c8: 14% d0: 31% d8: 33%) maybe 110??T
- P = Pressure, 10 bit 0.75 kPa
- I = id, 24-bit little-endian
- T = Temperature in C, offset -30
- ? = Unknown, mostly 0xffff
- C = Checksum, CRC-8 truncated poly 0x07 init 0x00

Notes from benppp:
The last bit in flags maybe indicates test/startup/reset condition,
it will be set for 2 minutes after power-up.
At least for a Zoe2 (Zoe2 original TPMS sensor: 407004CB0B) the 16 unknown bits are
9409 for stable pressure, 8C09 for pressure decrease and 9449 for something else.
Could be that the 4th/5th bit encode a pressure alert and the 10th bit indicates some other state.
*/
static int tpms_renault_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 160);
    // require 72 data bits
    if (packet_bits.bits_per_row[0] < 72) {
        return 0; // DECODE_ABORT_LENGTH
    }
    uint8_t *b = packet_bits.bb[0];

    // 0x83; 0x107 FOP-8; ATM-8; CRC-8P
    if (crc8(b, 8, 0x07, 0x00) != b[8]) {
        return 0; // DECODE_FAIL_MIC
    }

    int flags           = b[0] >> 2;
    unsigned id         = b[5] << 16 | b[4] << 8 | b[3]; // little-endian
    int pressure_raw    = (b[0] & 0x03) << 8 | b[1];
    double pressure_kpa = pressure_raw * 0.75;
    int temp_c          = b[2] - 30;
    int unknown         = b[7] << 8 | b[6]; // little-endian, fixed 0xffff?

    char flags_str[3];
    snprintf(flags_str, sizeof(flags_str), "%02x", flags);
    char id_str[7];
    snprintf(id_str, sizeof(id_str), "%06x", id);
    char code_str[5];
    snprintf(code_str, sizeof(code_str), "%04x", unknown);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Renault",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "flags",            "",             DATA_STRING, flags_str,
            "pressure_kPa",     "",             DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure_kpa,
            "temperature_C",    "",             DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temp_c,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_renault_decode() */
static int tpms_renault_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is 55 55 55 56 (inverted: aa aa aa a9)
    uint8_t const preamble_pattern[2] = {0xaa, 0xa9}; // 16 bits

    int row;
    unsigned bitpos;
    int ret    = 0;
    int events = 0;

    bitbuffer_invert(bitbuffer);

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                preamble_pattern, 16)) + 160 <=
                bitbuffer->bits_per_row[row]) {
            ret = tpms_renault_decode(decoder, bitbuffer, row, bitpos + 16);
            if (ret > 0)
                events += ret;
            bitpos += 15;
        }
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_kPa",
        "temperature_C",
        "mic",
        NULL,
};

r_device const tpms_renault = {
        .name        = "Renault TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,  // 12-13 samples @250k
        .long_width  = 52,  // FSK
        .reset_limit = 150, // Maximum gap size before End Of Message [us].
        .decode_fn   = &tpms_renault_callback,
        .fields      = output_fields,
};
