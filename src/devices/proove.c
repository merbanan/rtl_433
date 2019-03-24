/* Proove
 *
 * Tested devices:
 * Magnetic door & window sensor
 * "Proove" from 'Kjell & Company'
 * "Anslut" from "Jula"
 * "Telecontrol Plus" remote by "REV Ritter GmbH" (Germany) , model number "008341C-1"
 * "Nexa"
 * "Intertechno ITLS-16" (OEM model # "ITAPT-821")
 *
 * From http://elektronikforumet.com/wiki/index.php/RF_Protokoll_-_Proove_self_learning
 * Proove packet structure (32 bits):
 * HHHH HHHH HHHH HHHH HHHH HHHH HHGO CCEE
 * H = The first 26 bits are transmitter unique codes, and it is this code that the receiver “learns” to recognize.
 * G = Group code. Set to 0 for on, 1 for off.
 * O = On/Off bit. Set to 0 for on, 1 for off.
 * C = Channel bits.
 * E = Unit bits. Device to be turned on or off. Unit #1 = 00, #2 = 01, #3 = 10.
 * Physical layer.
 * Every bit in the packets structure is sent as two physical bits.
 * Where the second bit is the inverse of the first, i.e. 0 -> 01 and 1 -> 10.
 * Example: 10101110 is sent as 1001100110101001
 * The sent packet length is thus 64 bits.
 * A message is made up by a Sync bit followed by the Packet bits and ended by a Pause bit.
 * Every message is repeated four times.
 *
 * Copyright (C) 2016 Ask Jakobsen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

static int proove_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;

    /* Reject codes of wrong length */
    if (bitbuffer->bits_per_row[1] != 64)
      return 0;

    bitbuffer_t databits = {0};
    unsigned pos = bitbuffer_manchester_decode(bitbuffer, 1, 0, &databits, 64);

    /* Reject codes when Manchester decoding fails */
    if (pos != 64)
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
                     "model",         "",            DATA_STRING, _X("Proove-Security","Proove"),
                     "id",            "House Code",  DATA_INT, sensor_id,
                     "channel",       "Channel",     DATA_INT, channel_code,
                     "state",         "State",       DATA_STRING, on_bit ? "OFF" : "ON",
                     "unit",          "Unit",        DATA_INT, unit_bit,
                     "group",         "Group",       DATA_INT, group_code,
                      NULL);

    decoder_output_data(decoder, data);

    return 0;
}

static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "state",
    "unit",
    "group",
    NULL
};

r_device proove = {
    .name           = "Proove",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 270,
    .long_width     = 1300,
    .gap_limit      = 1500,
    .reset_limit    = 2800,
    .decode_fn      = &proove_callback,
    .disabled       = 0,
    .fields         = output_fields
};
