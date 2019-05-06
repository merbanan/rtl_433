/** @file
    Proove decoder.

    Copyright (C) 2016 Ask Jakobsen, Christian Juncker Brædstrup

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Proove/Nexa/Kaku decoder.
Might be similar to an x1527.
S.a. Kaku, Nexa.

Tested devices:
- Magnetic door & window sensor
  - "Proove" from 'Kjell & Company'
  - "Anslut" from "Jula"
  - "Telecontrol Plus" remote by "REV Ritter GmbH" (Germany) , model number "008341C-1"
  - "Nexa"
  - "Intertechno ITLS-16" (OEM model # "ITAPT-821")
  - Nexa - LMST-606

From http://elektronikforumet.com/wiki/index.php/RF_Protokoll_-_Proove_self_learning

Proove packet structure (32 bits or 36 bits with dimmer value):

    HHHH HHHH HHHH HHHH HHHH HHHH HHGO CCEE [DDDD]

- H = The first 26 bits are transmitter unique codes, and it is this code that the receiver “learns” to recognize.
- G = Group command. Set to 1 for on, 0 for off.
- O = On/Off bit. Set to 1 for on, 0 for off.
- C = Channel bits (inverted).
- E = Unit bits (inverted). Device to be turned on or off. Unit #1 = 00, #2 = 01, #3 = 10.
- D = Dimmer value (optional).

Physical layer:
Every bit in the packets structure is sent as two physical bits.
Where the second bit is the inverse of the first, i.e. 0 -> 01 and 1 -> 10.
Example: 10101110 is sent as 1001100110101001
The sent packet length is thus 64 bits.
A message is made up by a Sync bit followed by the Packet bits and ended by a Pause bit.
Every message is repeated about 5-15 times.
Packet gap is 10 ms.
*/

#include "decoder.h"

static int proove_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;

    /* Reject missing sync */
    if (bitbuffer->syncs_before_row[0] != 1)
        return DECODE_ABORT_EARLY;

    /* Reject codes of wrong length */
    if (bitbuffer->bits_per_row[0] != 64)
        return DECODE_ABORT_LENGTH;

    bitbuffer_t databits = {0};
    // note: not manchester encoded but actually ternary
    unsigned pos = bitbuffer_manchester_decode(bitbuffer, 0, 0, &databits, 80);
    bitbuffer_invert(&databits);

    /* Reject codes when Manchester decoding fails */
    if (pos != 64)
        return DECODE_ABORT_LENGTH;

    uint8_t *b = databits.bb[0];

    uint32_t id        = (b[0] << 18) | (b[1] << 10) | (b[2] << 2) | (b[3] >> 6); // ID 26 bits
    uint32_t group_cmd = (b[3] >> 5) & 1;
    uint32_t on_bit    = (b[3] >> 4) & 1;
    uint32_t channel   = ((b[3] >> 2) & 0x03) ^ 0x03; // inverted
    uint32_t unit      = (b[3] & 0x03) ^ 0x03;        // inverted

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, _X("Proove-Security","Proove"),
            "id",            "House Code",  DATA_INT,    id,
            "channel",       "Channel",     DATA_INT,    channel,
            "state",         "State",       DATA_STRING, on_bit ? "ON" : "OFF",
            "unit",          "Unit",        DATA_INT,    unit,
            "group",         "Group",       DATA_INT,    group_cmd,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "state",
        "unit",
        "group",
        NULL,
};

r_device proove = {
        .name        = "Proove / Nexa / KlikAanKlikUit Wireless Switch",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 270,  // 1:1
        .long_width  = 1300, // 1:5
        .sync_width  = 2700, // 1:10
        .tolerance   = 200,
        .gap_limit   = 1500,
        .reset_limit = 2800,
        .decode_fn   = &proove_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
