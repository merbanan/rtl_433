/** @file
    ThermoPro TX-2C Outdoor Thermometer.

    Copyright (C) 2023 igor@pele.tech.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ThermoPro TX-2C Outdoor Thermometer.

Example data:

    [00] { 7} 00
    [01] {45} 95 00 ff e0 a0 00
    [02] {45} 95 00 ff e0 a0 00
    [03] {45} 95 00 ff e0 a0 00
    [04] {45} 95 00 ff e0 a0 00
    [05] {45} 95 00 ff e0 a0 00
    [06] {45} 95 00 ff e0 a0 00
    [07] {45} 95 00 ff e0 a0 00
    [08] {36} 95 00 ff e0 a0

Data layout:

    II ZZ TTT UU Z

- I: 8 bit ID
- Z: 8 bit zeros
- T: 12 bit temperature, scale 10
- U: 8 bit unknown (constant)
- Z: 5 bit zeros

*/

static int thermopro_tx2c_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Compare first four bytes of rows that have 45 or 36 bits.
    int row = bitbuffer_find_repeated_row(bitbuffer, 4, 36);
    if (row < 0)
        return DECODE_ABORT_EARLY;
    uint8_t *b = bitbuffer->bb[row];

    if (bitbuffer->bits_per_row[row] > 45)
        return DECODE_ABORT_LENGTH;

    // No need to decode/extract values for simple test
    if ((!b[0] && !b[1] && !b[2] && !b[3])
            || (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff && b[3] == 0xff)) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00 or 0xFF");
        return DECODE_FAIL_SANITY;
    }

    int device   = (b[0]);
    int temp_raw = (int16_t)((b[2] << 8) | b[3]); // uses sign-extend
    float temp_c = (temp_raw >> 4) * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",            DATA_STRING, "Thermopro-TX2C",
            "id",            "Id",          DATA_INT,    device,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
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

r_device const thermopro_tx2c = {
        .name        = "ThermoPro TX-2C Thermometer",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1958,
        .long_width  = 3825,
        .gap_limit   = 3829,
        .reset_limit = 8643,
        .decode_fn   = &thermopro_tx2c_decode,
        .fields      = output_fields,
        .disabled    = 1, // default disabled because there is no checksum
};
