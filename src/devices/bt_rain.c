/**
 * Copyright (C) 2017 Timopen, cleanup by Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Based on the springfield.c code, there is a lack of samples and data
 * thus the decoder is disabled by default.
 *
 * nibble[0] and nibble[1] is the id, changes with every reset.
 * nibble[2] first bit is battery (0=OK).
 * nibble[3] bit 1 is tx button pressed.
 * nibble[3] bit 2 = below zero, subtract temperature with 1024. I.e. 11 bit 2's complement.
 * nibble[3](bit 3 and 4) + nibble[4] + nibble[5] is the temperature in Celsius with one decimal.
 * nibble[2](bit 2-4) + nibble[6] + nibble[7] is the rain rate, increases 25!? with every tilt of
 * the teeter (1.3 mm rain) after 82 tilts it starts over but carries the rest to the next round
 * e.g tilt 82 = 2 divide by 19.23 to get mm.
 * nibble[8] is checksum, have not figured it out yet. Last bit is sync? or included in checksum?.
 */

#include "decoder.h"

// Actually 37 bits for all but last transmission which is 36 bits
#define NUM_BITS 36

static int bt_rain_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    uint8_t *b;
    int row;
    int id, battery, rain, transmit, channel;
    int16_t temp_raw;
    float temp_c, rainrate;

    row = bitbuffer_find_repeated_row(bitbuffer, 4, NUM_BITS);

    if (bitbuffer->bits_per_row[row] != NUM_BITS && bitbuffer->bits_per_row[row] != NUM_BITS + 1)
        return 0;

    b = bitbuffer->bb[row];

    if (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff && b[3] == 0xff)
        return 0; // prevent false positive checksum

    id       = b[0];
    battery  = b[1] >> 7;
    channel  = ((b[1] & 0x30) >> 4) + 1; // either this or the rain top bits could be wrong
    transmit = (b[1] & 0x08) >> 3;

    temp_raw = ((b[1] & 0x07) << 13) | (b[2] << 5); // use sign extend
    temp_c = (temp_raw >> 5) * 0.1f;

    rain = ((b[1] & 0x07) << 4) | b[3]; // either b[1] or the channel above bould be wrong
    int rest = rain % 25;
    if (rest % 2)
        rain += ((rest / 2) * 2048);
    else
        rain += ((rest + 1) / 2) * 2048 + 12 * 2048;
    rainrate = rain * 0.052f; // 19.23mm per tip

    data = data_make(
            "model",            "",                 DATA_STRING, "Biltema-Rain",
            "id",               "ID",               DATA_INT, id,
            "channel",          "Channel",          DATA_INT, channel,
            "battery",          "Battery",          DATA_STRING, battery ? "LOW" : "OK",
            "transmit",         "Transmit",         DATA_STRING, transmit ? "MANUAL" : "AUTO",
            "temperature_C",    "Temperature",      DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "rainrate",         "Rain per hour",    DATA_FORMAT, "%.02f mm/h", DATA_DOUBLE, rainrate,
            NULL);
    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "battery",
    "transmit",
    "temperature_C",
    "rainrate",
    NULL
};

r_device bt_rain = {
    .name = "Biltema rain gauge",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 1940,
    .long_width     = 3900,
    .gap_limit      = 4100,
    .reset_limit    = 8800,
    .decode_fn      = &bt_rain_callback,
    .disabled       = 1,
    .fields         = output_fields
};
