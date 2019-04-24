/* Thermopro TP-12 Thermometer.
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

/*
A normal sequence for the TP12:

[00] {0} :
[01] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[02] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[03] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[04] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[05] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[06] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[07] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[08] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[09] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[10] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[11] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[12] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[13] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[14] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[15] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[16] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[17] {40} 38 73 21 bb 81 : 00111000 01110011 00100001 10111011 10000001

Layout appears to be:

[01] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
                              device   temp 1   temp     temp 2   checksum
                                       low bits 1   2    low bits
                                                hi bits

*/

#define BITS_IN_VALID_ROW 40

static int thermopro_tp12_sensor_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    int temp1_raw, temp2_raw, row;
    float temp1_c, temp2_c;
    uint8_t *bytes;
    unsigned int device;
    data_t *data;

    // The device transmits 16 rows, let's check for 3 matching.
    // (Really 17 rows, but the last one doesn't match because it's missing a trailing 1.)
    // Update for TP08: same is true but only 2 rows.
    row = bitbuffer_find_repeated_row(
            bitbuffer,
            (bitbuffer->num_rows > 5) ? 5 : 2,
            40);
    if (row < 0) {
        return 0;
    }

    bytes = bitbuffer->bb[row];
    if (!bytes[0] && !bytes[1] && !bytes[2] && !bytes[3]) {
        return 0; // reduce false positives
    }

    if (bitbuffer->bits_per_row[row] != 41)
        return 0;

    // Note: the device ID changes randomly each time you replace the battery, so we can't early out based on it.
    // This is probably to allow multiple devices to be used at once.  When you replace the receiver batteries
    // or long-press its power button, it pairs with the first device ID it hears.
    device = bytes[0];

    if (decoder->verbose) {
        // There is a mysterious checksum in bytes[4].  It may be the same as the checksum used by the TP-11,
        // which consisted of a lookup table containing, for each bit in the message, a byte to be xor-ed into
        // the checksum if the message bit was 1.  It should be possible to solve for that table using Gaussian
        // elimination, so dump some data so we can try this.

        // This format is easily usable by bruteforce-crc, after piping through | grep raw_data | cut -d':' -f2
        // bruteforce-crc didn't find anything, though - this may not be a CRC algorithm specifically.
        fprintf(stderr,"thermopro_tp12_raw_data:");
        bitrow_print(bytes, 40);
    }

    temp1_raw = ((bytes[2] & 0xf0) << 4) | bytes[1];
    temp2_raw = ((bytes[2] & 0x0f) << 8) | bytes[3];

    temp1_c = (temp1_raw - 200) * 0.1;
    temp2_c = (temp2_raw - 200) * 0.1;

    data = data_make(
            "model",            "",            DATA_STRING, _X("Thermopro-TP12","Thermopro TP12 Thermometer"),
            "id",               "Id",          DATA_INT,    device,
            "temperature_1_C",  "Temperature 1 (Food)", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp1_c,
            "temperature_2_C",  "Temperature 2 (Barbecue)", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp2_c,
            NULL);
    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "temperature_1_C",
    "temperature_2_C",
    NULL
};

r_device thermopro_tp12 = {
    .name          = "Thermopro TP08/TP12 thermometer",
    .modulation    = OOK_PULSE_PPM,
    .short_width   = 500,
    .long_width    = 1500,
    .gap_limit     = 2000,
    .reset_limit   = 4000,
    .decode_fn     = &thermopro_tp12_sensor_callback,
    .disabled      = 0,
    .fields        = output_fields,
};
