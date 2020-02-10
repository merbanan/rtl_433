/* Thermopro TP-11 Thermometer.
 *
 * Copyright (C) 2017 Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "decoder.h"

/* normal sequence of bit rows:
[00] {33} db 41 57 c2 80 : 11011011 01000001 01010111 11000010 1
[01] {33} db 41 57 c2 80 : 11011011 01000001 01010111 11000010 1
[02] {33} db 41 57 c2 80 : 11011011 01000001 01010111 11000010 1
[03] {32} db 41 57 c2 : 11011011 01000001 01010111 11000010

*/

static int thermopro_tp11_sensor_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    int temp_raw, row;
    float temp_c;
    bitrow_t *bb = bitbuffer->bb;
    unsigned int device, value;
    data_t *data;
    uint8_t ic;

    // Compare first four bytes of rows that have 32 or 33 bits.
    row = bitbuffer_find_repeated_row(bitbuffer, 2, 32);
    if (row < 0)
        return DECODE_ABORT_EARLY;

    if (bitbuffer->bits_per_row[row] > 33)
        return DECODE_ABORT_LENGTH;

    ic = lfsr_digest8_reflect(bb[row], 3, 0x51, 0x04);
    if (ic != bb[row][3]) {
        return DECODE_FAIL_MIC;
    }

    value = (bb[row][0] << 16) + (bb[row][1] << 8) + bb[row][2];
    device = value >> 12;

    temp_raw = value & 0xfff;
    temp_c = (temp_raw - 200) / 10.;

    data = data_make(
            "model",         "",            DATA_STRING, _X("Thermopro-TP11","Thermopro TP11 Thermometer"),
            "id",            "Id",          DATA_INT,    device,
            "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "mic",           "Integrity",   DATA_STRING, "CRC",
            NULL);
    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "temperature_C",
    "mic",
    NULL
};

r_device thermopro_tp11 = {
    .name          = "Thermopro TP11 Thermometer",
    .modulation    = OOK_PULSE_PPM,
    .short_width   = 500,
    .long_width    = 1500,
    .gap_limit     = 2000,
    .reset_limit   = 4000,
    .decode_fn     = &thermopro_tp11_sensor_callback,
    .disabled      = 0,
    .fields        = output_fields,
};
