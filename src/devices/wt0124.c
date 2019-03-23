/* WT0124 Pool Thermometer decoder.
 *
 * Copyright (C) 2018 Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 */

/*

5e       ba       9a       9f       e1       34
01011110 10111010 10011010 10011111 11100001 00110100
5555RRRR RRRRTTTT TTTTTTTT UUCCFFFF XXXXXXXX ????????


5 = constant 5
R = random power on id
T = 12 bits of temperature with 0x900 bias and scaled by 10
U = unk, maybe battery indicator (display is missing one though)
C = channel
F = constant F
X = xor checksum
? = unknown

*/

#include "decoder.h"


static int wt1024_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b; // bits of a row
    uint16_t sensor_rid;
    int16_t value;
    float temp_c;
    uint8_t channel;

    if (bitbuffer->bits_per_row[1] !=49)
        return 0;


    /* select row after preamble */
    b = bitbuffer->bb[1];

    /* Validate constant */
    if (b[0]>>4 != 0x5) {
        return 0;
    }

    /* Validate checksum */
    if ((b[0]^b[1]^b[2]^b[3]) != b[4])
        return 0;

    /* Get rid */
    sensor_rid = (b[0]&0x0F)<<4 | (b[1]&0x0F);

    /* Get temperature */
    temp_c = (float) ((((b[1]&0xF)<<8) | b[2])-0x990) / 10.0;

    /* Get channel */
    channel = ((b[3]>>4) & 0x3);

    /* unk */
    value = b[5];

    if (decoder->verbose) {
        fprintf(stderr, "wt1024_callback:");
        bitbuffer_print(bitbuffer);
    }

    data = data_make(
            "model", "", DATA_STRING, _X("WT0124-Pool","WT0124 Pool Thermometer"),
            _X("id","rid"),    "Random ID", DATA_INT,    sensor_rid,
            "channel",       "Channel",     DATA_INT,    channel,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "mic",      "Integrity",      DATA_STRING, "CHECKSUM",
            "data",  "Data", DATA_INT,    value,
            NULL);

    decoder_output_data(decoder, data);

    // Return 1 if message successfully decoded
    return 1;
}

/*
 * List of fields that may appear in the output
 *
 * Used to determine what fields will be output in what
 * order for this device when using -F csv.
 *
 */
static char *output_fields[] = {
    "model",
    "rid", // TODO: delete this
    "id",
    "channel",
    "temperature_C",
    "mic",
    "data",
    NULL
};


r_device wt1024 = {
    .name          = "WT0124 Pool Thermometer",
    .modulation    = OOK_PULSE_PWM,
    .short_width   = 680,
    .long_width    = 1850,
    .reset_limit   = 30000,
    .gap_limit     = 4000,
    .sync_width    = 10000,
    .decode_fn     = &wt1024_callback,
    .disabled      = 0,
    .fields        = output_fields,
};
