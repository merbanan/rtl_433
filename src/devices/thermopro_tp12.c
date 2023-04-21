/** @file
    ThermoPro TP-12 Thermometer.

    Copyright (C) 2017 Google Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ThermoPro TP-12 Thermometer.

A normal sequence for the TP12:

    [00] {0} :
    [01] {41} 38 73 21 bb 81 80
    [02] {41} 38 73 21 bb 81 80
    [03] {41} 38 73 21 bb 81 80
    [04] {41} 38 73 21 bb 81 80
    [05] {41} 38 73 21 bb 81 80
    [06] {41} 38 73 21 bb 81 80
    [07] {41} 38 73 21 bb 81 80
    [08] {41} 38 73 21 bb 81 80
    [09] {41} 38 73 21 bb 81 80
    [10] {41} 38 73 21 bb 81 80
    [11] {41} 38 73 21 bb 81 80
    [12] {41} 38 73 21 bb 81 80
    [13] {41} 38 73 21 bb 81 80
    [14] {41} 38 73 21 bb 81 80
    [15] {41} 38 73 21 bb 81 80
    [16] {41} 38 73 21 bb 81 80
    [17] {40} 38 73 21 bb 81

Layout appears to be:

    [01] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
                                  device   temp 1   temp     temp 2   checksum
                                           low bits 1   2    low bits
                                                    hi bits

*/

#define BITS_IN_VALID_ROW 41

static int thermopro_tp12_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int temp1_raw, temp2_raw, row;
    float temp1_c, temp2_c;
    uint8_t *bytes;
    unsigned int device;
    data_t *data;
    uint8_t ic;

    // The device transmits 16 rows, let's check for 3 matching.
    // (Really 17 rows, but the last one doesn't match because it's missing a trailing 1.)
    // Update for TP08: same is true but only 2 rows.
    row = bitbuffer_find_repeated_prefix(
            bitbuffer,
            (bitbuffer->num_rows > 5) ? 5 : 2,
            BITS_IN_VALID_ROW - 1); // allow 1 bit less to also match the last row
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    bytes = bitbuffer->bb[row];
    if (!bytes[0] && !bytes[1] && !bytes[2] && !bytes[3]) {
        return DECODE_ABORT_EARLY; // reduce false positives
    }

    if (bitbuffer->bits_per_row[row] != BITS_IN_VALID_ROW)
        return DECODE_ABORT_LENGTH;

    ic = lfsr_digest8_reflect(bytes, 4, 0x51, 0x04);
    if (ic != bytes[4]) {
        return DECODE_FAIL_MIC;
    }
    // Note: the device ID changes randomly each time you replace the battery, so we can't early out based on it.
    // This is probably to allow multiple devices to be used at once.  When you replace the receiver batteries
    // or long-press its power button, it pairs with the first device ID it hears.
    device = bytes[0];

    temp1_raw = ((bytes[2] & 0xf0) << 4) | bytes[1];
    temp2_raw = ((bytes[2] & 0x0f) << 8) | bytes[3];

    temp1_c = (temp1_raw - 200) * 0.1f;
    temp2_c = (temp2_raw - 200) * 0.1f;

    /* clang-format off */
    data = data_make(
            "model",            "",            DATA_STRING, "Thermopro-TP12",
            "id",               "Id",          DATA_INT,    device,
            "temperature_1_C",  "Temperature 1 (Food)", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp1_c,
            "temperature_2_C",  "Temperature 2 (Barbecue)", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp2_c,
            "mic",              "Integrity",   DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_1_C",
        "temperature_2_C",
        "mic",
        NULL,
};

r_device const thermopro_tp12 = {
        .name        = "ThermoPro TP08/TP12/TP20 thermometer",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 500,
        .long_width  = 1500,
        .gap_limit   = 2000,
        .reset_limit = 4000,
        .decode_fn   = &thermopro_tp12_decode,
        .fields      = output_fields,
};
