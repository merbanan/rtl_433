/* Nexa
 *
 * Tested devices:
 * Magnetic sensor - LMST-606
 *
 * This device is very similar to the proove magnetic sensor.
 * The proove decoder will capture the OFF-state but not the ON-state
 * since the Nexa uses two different bit lengths for ON and OFF.
 *
 * Copyright (C) 2017 Christian Juncker Brædstrup
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

static int nexa_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;

    /* Reject codes of wrong length */
    if (bitbuffer->bits_per_row[1] != 64 && bitbuffer->bits_per_row[1] != 72)
      return 0;


    bitbuffer_t databits = {0};
    unsigned pos = bitbuffer_manchester_decode(bitbuffer, 1, 0, &databits, 80);

    /* Reject codes when Manchester decoding fails */
    if (pos != 64 && pos != 72)
      return 0;

    bitrow_t *bb = databits.bb;
    uint8_t *b = bb[0];

    uint32_t sensor_id = (b[0] << 18) | (b[1] << 10) | (b[2] << 2) | (b[3]>>6); // ID 26 bits
    uint32_t group_code = (b[3] >> 5) & 1;
    uint32_t on_bit = (b[3] >> 4) & 1;
    uint32_t channel_code = (b[3] >> 2) & 0x03;
    uint32_t unit_bit = (b[3] & 0x03);

    /* Get time now */

    data = data_make(
                     "model",         "",            DATA_STRING, _X("Nexa-Security","Nexa"),
                     "id",            "House Code",  DATA_INT, sensor_id,
                     "group",         "Group",       DATA_INT, group_code,
                     "channel",       "Channel",     DATA_INT, channel_code,
                     "state",         "State",       DATA_STRING, on_bit ? "OFF" : "ON",
                     "unit",          "Unit",        DATA_INT, unit_bit,
                      NULL);

    decoder_output_data(decoder, data);

    return 0;
}

static char *output_fields[] = {
    "model",
    "id",
    "group",
    "channel",
    "state",
    "unit",
    NULL
};

r_device nexa = {
    .name           = "Nexa",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 270,
    .long_width     = 1300,
    .gap_limit      = 1500,
    .reset_limit    = 2800,
    .decode_fn      = &nexa_callback,
    .disabled       = 0,
    .fields         = output_fields
};
