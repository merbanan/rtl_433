/** @file
    inFactory outdoor temperature and humidity sensor.

    Copyright (C) 2017 Sirius Weiß <siriuz@gmx.net>
    Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
inFactory Outdoor sensor transmits data temperature, humidity.

Also NC-3982-913 from Pearl (for FWS-686 station).

Transmissions also includes an id. The sensor transmits
every 60 seconds 6 packets.

    0000 1111 | 0011 0000 | 0101 1100 | 1110 0111 | 0110 0001
    xxxx xxxx | cccc cccc | tttt tttt | tttt hhhh | hhhh ??nn

- x: ID // changes on battery switch
- c: Unknown Checksum (changes on every transmit if the other values are different)
- h: Humidity // BCD-encoded, each nibble is one digit
- t: Temperature   // in °F as binary number with one decimal place + 90 °F offset
- n: Channel // Channel number 1 - 3

Usage:

    # rtl_433 -f 434052000 -R 91 -F json:log.json

Payload looks like this:

    [00] { 4} 00             : 0000
    [01] {40} 0f 30 5c e7 61 : 00001111 00110000 01011100 11100111 01100001

First row is actually the preamble part. This is added to make the signal more unique.
*/

#include "decoder.h"

static int infactory_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitrow_t *bb;
    data_t *data;
    uint8_t *b;
    int id, humidity, temp_raw, channel;
    float temp_f;

    if (bitbuffer->bits_per_row[0] != 4) {
        return DECODE_ABORT_LENGTH;
    }
    if (bitbuffer->bits_per_row[1] != 40) {
        return DECODE_ABORT_LENGTH;
    }
    bb = bitbuffer->bb;

    /* Check that 4 bits of preamble is 0 */
    b = bb[0];
    if (b[0]&0x0F)
        return DECODE_ABORT_EARLY;

    /* Check that last 4 bits of message is 0x1 */
    b = bb[1];
    if (!(b[4]&0x0F))
        return DECODE_ABORT_EARLY;

    id       = b[0];
    humidity = (b[3] & 0x0F) * 10 + (b[4] >> 4); // BCD
    temp_raw = (b[2] << 4) | (b[3] >> 4);
    temp_f   = (float)temp_raw * 0.1 - 90;
    channel  = b[4] & 0x03;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, _X("inFactory-TH","inFactory sensor"),
            "id",               "ID",           DATA_INT, id,
            "channel",          "Channel",      DATA_INT, channel,
            "temperature_F",    "Temperature",  DATA_FORMAT, "%.02f °F", DATA_DOUBLE, temp_f,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "temperature_F",
        "humidity",
        NULL,
};

r_device infactory = {
        .name        = "inFactory, FreeTec NC-3982-913 temperature humidity sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1850,
        .long_width  = 4050,
        .gap_limit   = 4000, // Maximum gap size before new row of bits [us]
        .reset_limit = 8500, // Maximum gap size before End Of Message [us].
        .tolerance   = 1000,
        .decode_fn   = &infactory_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
