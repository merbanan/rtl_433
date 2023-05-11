/** @file
    ThermoPro TP-11 Thermometer.

    Copyright (C) 2017 Google Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ThermoPro TP-11 Thermometer.

normal sequence of bit rows:

    [00] {33} db 41 57 c2 80
    [01] {33} db 41 57 c2 80
    [02] {33} db 41 57 c2 80
    [03] {32} db 41 57 c2

*/

static int thermopro_tp11_sensor_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Compare first four bytes of rows that have 32 or 33 bits.
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 32);
    if (row < 0)
        return DECODE_ABORT_EARLY;
    uint8_t *b = bitbuffer->bb[row];

    if (bitbuffer->bits_per_row[row] > 33)
        return DECODE_ABORT_LENGTH;

    uint8_t ic = lfsr_digest8_reflect(b, 3, 0x51, 0x04);
    if (ic != b[3]) {
        return DECODE_FAIL_MIC;
    }

    // No need to decode/extract values for simple test
    if ((!b[0] && !b[1] && !b[2] && !b[3])
            || (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff && b[3] == 0xff)) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00 or 0xFF");
        return DECODE_FAIL_SANITY;
    }

    int device   = (b[0] << 4) | (b[1] >> 4);
    int temp_raw = ((b[1] & 0x0f) << 8) | b[2];
    float temp_c = (temp_raw - 200) * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",            DATA_STRING, "Thermopro-TP11",
            "id",            "Id",          DATA_INT,    device,
            "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "mic",           "Integrity",   DATA_STRING, "CRC",
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

r_device const thermopro_tp11 = {
        .name        = "Thermopro TP11 Thermometer",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 500,
        .long_width  = 1500,
        .gap_limit   = 2000,
        .reset_limit = 4000,
        .decode_fn   = &thermopro_tp11_sensor_callback,
        .fields      = output_fields,
};
