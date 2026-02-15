/** @file
    ThermoPro TP211B Thermometer.

    Copyright (C) 2026, Ali Rahimi.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ThermoPro TP211B 915 MHz FSK temperature sensor.

Based on [this issue](https://github.com/merbanan/rtl_433/issues/3435), and thanks to the
analysis conducted there.

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
- CC:  {16} Unknown trailing bytes, possibly checksum
- Followed by trailing d2 d2 d2 d2 d2 00 00 (not used).
*/

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

    if ((!b[0] && !b[1] && !b[2] && !b[3] && !b[4])
            || (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff && b[3] == 0xff && b[4] == 0xff)) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00 or 0xFF");
        return DECODE_FAIL_SANITY;
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
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_C",
        NULL,
};

r_device const thermopro_tp211b = {
        .name        = "ThermoPro TP211B Thermometer",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 105,
        .long_width  = 105,
        .reset_limit = 1500,
        .decode_fn   = &thermopro_tp211b_decode,
        .disabled    = 1, // default disabled because we're not checking the checksum.
        .fields      = output_fields,
};
