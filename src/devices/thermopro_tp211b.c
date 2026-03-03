/** @file
    ThermoPro TP211B Thermometer.

    Copyright (C) 2026, Ali Rahimi, Bruno OCTAU, Christian W. Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ThermoPro TP211B Thermometer.

RF:
- 915 MHz FSK temperature sensor.

Based on issue #3435 open by \@splobsterman, and thanks to the analysis conducted there by Ali, Bruno, Christian
And contributors with lot of samples from \@splobsterman, \@moryckaz, and Ali

Flex decoder:

    rtl_433 -f 915M -X "n=tp211b,m=FSK_PCM,s=105,l=105,r=1500,preamble=552dd4"

Data layout after preamble:

    Byte Position   0  1  2  3  4  5  6  7
    Sample          01 1e d6 03 6c aa 14 ff
    Sample          01 1e d6 02 fa aa c4 1e
                    II II II fT TT aa CC CC

- III: {24} Sensor ID
- f:   {4}  Flags or unused, always 0
- TTT: {12} Temperature, raw value, °C = (raw - 500) / 10
- aa:  {8}  Fixed value 0xAA
- CC:  {16} Checksum, XOR bit with a specific WORD to get the 16 bit values, and final XOR with 0x411B, see table below.
- Followed by trailing d2 d2 d2 d2 d2 00 00 (not used).

XOR Table by bit position into the frame:

    0xC881 [ Bit 0 Byte 0 ]
    0xC441 [ Bit 1 Byte 0 ]
    0xC221 [ Bit 2 Byte 0 ]
    0xC111 [ Bit 3 Byte 0 ]

    0xC089 [ Bit 4 Byte 0 ]
    0xC045 [ Bit 5 Byte 0 ]
    0xC023 [ Bit 6 Byte 0 ]
    0xC010 [ Bit 7 Byte 0 ]

    0xC01F [ Bit 8 Byte 1 ]
    0xC00E [ Bit 9 Byte 1 ]
    0x6007 [ Bit 10 Byte 1 ]
    0x9002 [ Bit 11 Byte 1 ]

    0x4801 [ Bit 12 Byte 1 ]
    0x8401 [ Bit 13 Byte 1 ]
    0xE201 [ Bit 14 Byte 1 ]
    0xD101 [ Bit 15 Byte 1 ]

    0xDE01 [ Bit 16 Byte 2 ]
    0xCF01 [ Bit 17 Byte 2 ]
    0xC781 [ Bit 18 Byte 2 ]
    0xC3C1 [ Bit 19 Byte 2 ]

    0xC1E1 [ Bit 20 Byte 2 ]
    0xC0F1 [ Bit 21 Byte 2 ]
    0xC079 [ Bit 22 Byte 2 ]
    0xC03D [ Bit 23 Byte 2 ]

    0xC029 [ Bit 24 Byte 3 ]
    0xC015 [ Bit 25 Byte 3 ]
    0xC00B [ Bit 26 Byte 3 ]
    0xC004 [ Bit 27 Byte 3 ]

    0x6002 [ Bit 28 Byte 3 ]
    0x3001 [ Bit 29 Byte 3 ]
    0xB801 [ Bit 30 Byte 3 ]
    0xFC01 [ Bit 31 Byte 3 ]

    0xE801 [ Bit 32 Byte 4 ]
    0xD401 [ Bit 33 Byte 4 ]
    0xCA01 [ Bit 34 Byte 4 ]
    0xC501 [ Bit 35 Byte 4 ]

    0xC281 [ Bit 36 Byte 4 ]
    0xC141 [ Bit 37 Byte 4 ]
    0xC0A1 [ Bit 38 Byte 4 ]
    0xC051 [ Bit 39 Byte 4 ]

    0xC061 [ Bit 40 Byte 5 ]
    0xC031 [ Bit 41 Byte 5 ]
    0xC019 [ Bit 42 Byte 5 ]
    0xC00D [ Bit 43 Byte 5 ]

    0xC007 [ Bit 44 Byte 5 ]
    0xC002 [ Bit 45 Byte 5 ]
    0x6001 [ Bit 46 Byte 5 ]
    0x9001 [ Bit 47 Byte 5 ]

*/

static uint16_t tp211b_checksum(uint8_t const *b)
{
    static uint16_t const xor_table[] = {
            0xC881, 0xC441, 0xC221, 0xC111, 0xC089, 0xC045, 0xC023, 0xC010,
            0xC01F, 0xC00E, 0x6007, 0x9002, 0x4801, 0x8401, 0xE201, 0xD101,
            0xDE01, 0xCF01, 0xC781, 0xC3C1, 0xC1E1, 0xC0F1, 0xC079, 0xC03D,
            0xC029, 0xC015, 0xC00B, 0xC004, 0x6002, 0x3001, 0xB801, 0xFC01,
            0xE801, 0xD401, 0xCA01, 0xC501, 0xC281, 0xC141, 0xC0A1, 0xC051,
            0xC061, 0xC031, 0xC019, 0xC00D, 0xC007, 0xC002, 0x6001, 0x9001};
    uint16_t checksum = 0x411b;       // modified below with final checksum.
    for (int n = 0; n < 6; n++) {     // iterate over the bytes in the row.
        for (int i = 0; i < 8; i++) { // iterate over the bits in the byte.
            const int bit = (b[n] << (i + 1)) & 0x100;
            if (bit) {
                checksum ^= xor_table[(n * 8) + i];
            }
        }
    }
    return checksum;
}

static int thermopro_tp211b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0x55, 0x2d, 0xd4};
    uint8_t b[8];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    const int msg_len = bitbuffer->bits_per_row[0];

    int offset = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= msg_len) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    if ((msg_len - offset) < 64) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 8 * 8);

    // Sanity check: byte 5 should be fixed 0xAA
    if (b[5] != 0xaa) {
        decoder_log(decoder, 1, __func__, "Fixed byte mismatch (expected 0xAA at byte 5)");
        return DECODE_FAIL_SANITY;
    }

    if ((!b[0] && !b[1] && !b[2] && !b[3] && !b[4]) || (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff && b[3] == 0xff && b[4] == 0xff)) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00 or 0xFF");
        return DECODE_FAIL_SANITY;
    }

    const uint16_t checksum_calc     = tp211b_checksum(b);
    const uint16_t checksum_from_row = b[6] << 8 | b[7];
    if (checksum_from_row != checksum_calc) {
        decoder_logf(decoder, 2, __func__, "Checksum error, calculated %04x, expected %04x", checksum_calc, checksum_from_row);
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 2, __func__, b, 64, "MSG");

    int id       = (b[0] << 16) | (b[1] << 8) | b[2];
    int temp_raw = ((b[3] & 0x0f) << 8) | b[4];
    float temp_c = (temp_raw - 500) * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",            DATA_STRING, "ThermoPro-TP211B",
            "id",            "Id",          DATA_FORMAT, "%06x",   DATA_INT,    id,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)temp_c,
            "mic",           "Integrity",   DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "mic",
        NULL,
};

r_device const thermopro_tp211b = {
        .name        = "ThermoPro TP211B Thermometer",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 105,
        .long_width  = 105,
        .reset_limit = 1500,
        .decode_fn   = &thermopro_tp211b_decode,
        .fields      = output_fields,
};
