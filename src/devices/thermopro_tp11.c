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

The code below checks that at least three rows are the same and
that the validation code is correct for the known device ids.
*/

static int valid(unsigned data, unsigned check) {
    // This table is computed for device ids 0xb34 and 0xdb4. Since the code
    // appear to be linear, it is most likely correct also for device ids
    // 0 and 0xb34^0xdb4 == 0x680. It needs to be updated for others, the
    // values starting at table[12] are most likely wrong for other devices.
    static int table[] = {
        0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x51, 0xa2,
        0x15, 0x2a, 0x54, 0xa8, 0x00, 0x00, 0xed, 0x00,
        0x00, 0x00, 0x00, 0x37, 0x00, 0x00, 0x00, 0x00};
    for(int i=0;i<24;i++) {
        if (data & (1 << i)) check ^= table[i];
    }
    return check == 0;
}

static int thermopro_tp11_sensor_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    int temp_raw, row;
    float temp_c;
    bitrow_t *bb = bitbuffer->bb;
    unsigned int device, value;
    data_t *data;

    // Compare first four bytes of rows that have 32 or 33 bits.
    row = bitbuffer_find_repeated_row(bitbuffer, 2, 32);
    if (row < 0)
        return 0;

    if (bitbuffer->bits_per_row[row] > 33)
        return 0;

    value = (bb[row][0] << 16) + (bb[row][1] << 8) + bb[row][2];
    device = value >> 12;

    // Validate code for known devices.
    if ((device == 0xb34 || device == 0xdb4 ) && !valid(value, bb[row][3]))
        return 0;

    temp_raw = value & 0xfff;
    temp_c = (temp_raw - 200) / 10.;

    data = data_make(
            "model",         "",            DATA_STRING, _X("Thermopro-TP11","Thermopro TP11 Thermometer"),
            "id",            "Id",          DATA_INT,    device,
            "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            NULL);
    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "temperature_C",
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
