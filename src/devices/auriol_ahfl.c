/** @file
    Auriol AHFL 433B2 IPX4

    Copyright (C) 2021 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Lidl Auriol Auriol AHFL 433B2 IPX4

[00] {42} f2  00     ef 7c 41 40 : 11110010 00000000 11101111 01111100 01000001 01

          II [BXCC]T TT HH FS [SS--]

  42 bit message

  I - id, 8 bits
  B - batter, 1 bit
  X - tx-button, 1 bit (might not work)
  C - channel, 2 bits
  T - temperature, 12 bits
  H - humidity, 7 bits data, 1 bit 0
  F - always 0x4 (0100)
  S - nibble sum, 6 bits
*/

#include "decoder.h"

static int auriol_ahfl_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int row;
    int id;
    int channel;
    int tx_button;
    int battery_ok;
    int temp_raw;
    float temp_c;
    int humidity;
    int nibble_sum, checksum;

    row = bitbuffer_find_repeated_row(bitbuffer, 2, 42);
    if (row < 0) {
        return DECODE_ABORT_EARLY; // no repeated row found
    }

    if (bitbuffer->bits_per_row[row] != 42)
        return DECODE_ABORT_LENGTH;

    b = bitbuffer->bb[row];

    /* Check fixed message values */
    if (((b[4] & 0xF0) != 0x40) || ((b[3] & 0x1) != 0x0)) {
        return DECODE_FAIL_SANITY;
    }

    // calculate nibble sum
    nibble_sum = (b[0] & 0xF) + (b[0] >> 4) +
                 (b[1] & 0xF) + (b[1] >> 4) +
                 (b[2] & 0xF) + (b[2] >> 4) +
                 (b[3] & 0xF) + (b[3] >> 4) +
                 (b[4] >> 4);
    checksum = ((b[4] & 0xF) << 2) | ((b[5] & 0xC0) >> 6);

    // check 6 bits of nibble sum
    if ((nibble_sum & 0x3F) != checksum)
        return DECODE_FAIL_MIC;

    id         = b[0];
    battery_ok = b[1] >> 7;
    channel    = (b[1] & 0x30) >> 4;
    tx_button  = (b[1] & 0x40) >> 6;
    temp_raw   = (int16_t)(((b[1] & 0x0f) << 12) | (b[2] << 4)); // uses sign extend
    temp_c     = (temp_raw >> 4) * 0.1f;
    humidity   = b[3] >> 1;

    /* clang-format off */
    data = data_make(
            "model",            "",                  DATA_STRING, "Auriol-AHFL",
            "id",               "",                  DATA_INT,    id,
            "channel",          "Channel",           DATA_INT,    channel + 1,
            "battery_ok",       "Battery",           DATA_INT,    battery_ok,
            "button",           "Button",            DATA_INT,    tx_button,
            "temperature_C",    "Temperature",       DATA_FORMAT, "%.1f C",  DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",          DATA_FORMAT, "%d %%", DATA_INT, humidity,
            "mic",              "Integrity",         DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "button",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device const auriol_ahfl = {
        .name        = "Auriol AHFL temperature/humidity sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2100,
        .long_width  = 4150,
        .sync_width  = 0, // No sync bit used
        .gap_limit   = 4248,
        .reset_limit = 9150,
        .decode_fn   = &auriol_ahfl_decode,
        .fields      = output_fields,
};
