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
 * Nibble[0] and nibble[1] is the id, changes with every reset.
 * Nibble[2] first bit is battery (0=OK).
 * Nibble[3] bit 1 is tx button pressed.
 * Nibble[3] bit 2 = below zero, subtract temperature with 1024.
 * Nibble[3](bit 3 and 4) + nibble[4] + nibble[5] is the temperature in Celsius with one decimal.
 * Nibble[2](bit 2-4) + nibble[6] + nibble[7] is the rain rate, increases 25!? with every tilt of
 * the teeter (1.3 mm rain) after 82 tilts it starts over but carries the rest to the next round
 * e.g tilt 82 = 2 divide by 19.23 to get mm.
 * Nibble[8] is checksum, have not figured it out yet. Last bit is sync? or included in checksum?.
 */

#include "decoder.h"

// Actually 37 bits for all but last transmission which is 36 bits
#define	NUM_BITS	36

static int bt_rain_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    int ret = 0;
    int row, i;
    int nibble[NUM_BITS/4+1];
    int sid, battery, rain, transmit, channel, temp;
    float tempC, rainrate;
    data_t *data;
    unsigned tmpData;

    row = bitbuffer_find_repeated_row(bitbuffer, 4, NUM_BITS);

    if(bitbuffer->bits_per_row[row] == NUM_BITS || bitbuffer->bits_per_row[row] == NUM_BITS+1) {
        tmpData = (bitbuffer->bb[row][0] << 24) + (bitbuffer->bb[row][1] << 16) + (bitbuffer->bb[row][2] << 8) + bitbuffer->bb[row][3];
        if (tmpData == 0xffffffff)
            return 0; // prevent false positive checksum

        for(i = 0; i < (NUM_BITS/4); i++) {
            if((i & 0x01) == 0x01)
                nibble[i] = bitbuffer->bb[row][i >> 1] & 0x0f;
            else
                nibble[i] = bitbuffer->bb[row][i >> 1] >> 0x04;
        }

        sid = (nibble[0] << 4) + nibble[1];
        battery = (nibble[2] >> 3) & 0x01;
        transmit = (nibble[3] >> 3) & 0x01;
        channel = (nibble[2] & 0x03) + 1;
        temp = (((nibble[3]&0x03) << 8) + (nibble[4] << 4) + nibble[5]);
        if(nibble[3]&0x04) temp = temp - 0x1000;
        tempC = temp / 10.0;
        rain = (((nibble[2]&0x07)<<8) + (nibble[6] << 4) + nibble[7]);
        int rest = rain % 25;
        if(rest % 2)
            rain + ((rest / 2) * 2048);
        else
            (rain + ((rest+1) / 2) * 2048) + 24576;
        rainrate = rain/19.23F;
        data = data_make(
            "model",        "",         DATA_STRING, "Biltema rain gauge",
            "sid",          "SID",      DATA_INT, sid,
            "channel",      "Channel",  DATA_INT, channel,
            "battery",      "Battery",  DATA_STRING, battery ? "LOW" : "OK",
            "transmit",     "Transmit", DATA_STRING, transmit ? "MANUAL" : "AUTO",
            "temperature_C","Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE,tempC,
            "rainrate",     "Rainrate/hour", DATA_FORMAT, "%.02f mm/h", DATA_DOUBLE,rainrate,
            NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    return 0;
}

static char *output_fields[] = {
    "model",
    "sid",
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
