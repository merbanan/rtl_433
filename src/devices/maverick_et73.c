/* Maverick ET-73
 *
 * Copyright (C) 2018 Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "decoder.h"

/*
Based on TP12 code

[00] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[01] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[02] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[03] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[04] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[05] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[06] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[07] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[08] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[09] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[10] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[11] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[12] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100
[13] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100


Layout appears to be:
          II 11 12 22 XX XX
[01] {48} 68 00 01 0b 90 fc : 01101000 00000000 00000001 00001011 10010000 11111100

I = random id
1 = temperature sensor 1 12 bits
2 = temperature sensor 2 12 bits
X = unknown, checksum maybe ?

*/


static int maverick_et73_sensor_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    int temp1_raw, temp2_raw, row;
    float temp1_c, temp2_c;
    uint8_t *bytes;
    unsigned int device;
    data_t *data;

    // The device transmits many rows, let's check for 3 matching.
    row = bitbuffer_find_repeated_row(bitbuffer, 3, 48);
    if (row < 0) {
        return 0;
    }

    bytes = bitbuffer->bb[row];
    if (!bytes[0] && !bytes[1] && !bytes[2] && !bytes[3]) {
        return 0; // reduce false positives
    }

    if (bitbuffer->bits_per_row[row] != 48)
        return 0;

    device = bytes[0];

    if (decoder->verbose) {
        fprintf(stderr,"maverick_et73_raw_data:");
        bitrow_print(bytes, 48);
    }

    temp1_raw = (bytes[1] << 4) | ((bytes[2] & 0xf0) );
    temp2_raw = ((bytes[2] & 0x0f) << 8) | bytes[3];

    temp1_c = temp1_raw * 0.1;
    temp2_c = temp2_raw * 0.1;

    data = data_make(
            "model",            "",                 DATA_STRING, _X("Maverick-ET73","Maverick ET73"),
            _X("id","rid"),              "Random Id",        DATA_INT, device,
            "temperature_1_C",  "Temperature 1",    DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp1_c,
            "temperature_2_C",  "Temperature 2",    DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp2_c,
            NULL);
    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "rid", // TODO: delete this
    "id",
    "temperature_1_C",
    "temperature_2_C",
    NULL
};

r_device maverick_et73 = {
    .name          = "Maverick et73",
    .modulation    = OOK_PULSE_PPM,
    .short_width   = 1050,
    .long_width    = 2050,
    .gap_limit     = 2070,
    .reset_limit   = 4040,
    .decode_fn     = &maverick_et73_sensor_callback,
    .disabled      = 0,
    .fields        = output_fields,
};
