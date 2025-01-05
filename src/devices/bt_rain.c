/** @file
    Biltema-Rain sensor.

    Copyright (C) 2017 Timopen, cleanup by Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Biltema-Rain sensor.

Based on the springfield.c code, there is a lack of samples and data
thus the decoder is disabled by default.

- nibble[0] and nibble[1] is the id, changes with every reset.
- nibble[2] first bit is battery (0=OK).
- nibble[3] bit 1 is tx button pressed.
- nibble[3] bit 2 = below zero, subtract temperature with 1024. I.e. 11 bit 2's complement.
- nibble[3](bit 3 and 4) + nibble[4] + nibble[5] is the temperature in Celsius with one decimal.
- nibble[2](bit 2-4) + nibble[6] + nibble[7] is the rain rate, increases 25!? with every tilt of
  the teeter (1.3 mm rain) after 82 tilts it starts over but carries the rest to the next round
  e.g tilt 82 = 2 divide by 19.23 to get mm.
- nibble[8] is checksum, have not figured it out yet. Last bit is sync? or included in checksum?.
*/

#include "decoder.h"

// Actually 37 bits for all but last transmission which is 36 bits
#define NUM_BITS 36

static int bt_rain_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int row;
    int id, battery, rain, button, channel;
    int temp_raw;
    float temp_c, rainrate;

    row = bitbuffer_find_repeated_row(bitbuffer, 4, NUM_BITS);
    if (row < 0)
        return DECODE_ABORT_EARLY;

    if (bitbuffer->bits_per_row[row] != NUM_BITS && bitbuffer->bits_per_row[row] != NUM_BITS + 1)
        return DECODE_ABORT_LENGTH;

    b = bitbuffer->bb[row];

    if (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff && b[3] == 0xff)
        return DECODE_FAIL_SANITY; // prevent false positive checksum

    id      = b[0];
    battery = b[1] >> 7;
    channel = ((b[1] & 0x30) >> 4) + 1; // either this or the rain top bits could be wrong
    button  = (b[1] & 0x08) >> 3;

    temp_raw = (int16_t)(((b[1] & 0x07) << 13) | (b[2] << 5)); // uses sign extend
    temp_c   = (temp_raw >> 5) * 0.1f;

    rain     = ((b[1] & 0x07) << 4) | b[3]; // either b[1] or the channel above bould be wrong
    int rest = rain % 25;
    if (rest % 2)
        rain += ((rest / 2) * 2048);
    else
        rain += ((rest + 1) / 2) * 2048 + 12 * 2048;
    rainrate = rain * 0.052f; // 19.23mm per tip

    /* clang-format off */
    data = NULL;
    data = data_str(data, "model",            "",                 NULL,         "Biltema-Rain");
    data = data_int(data, "id",               "ID",               NULL,         id);
    data = data_int(data, "channel",          "Channel",          NULL,         channel);
    data = data_int(data, "battery_ok",       "Battery",          NULL,         !battery);
    data = data_str(data, "transmit",         "Transmit",         NULL,         button ? "MANUAL" : "AUTO"); // TODO: delete this
    data = data_dbl(data, "temperature_C",    "Temperature",      "%.1f C",     temp_c);
    data = data_dbl(data, "rain_rate_mm_h",   "Rain per hour",    "%.2f mm/h",  rainrate);
    data = data_int(data, "button",           "Button",       NULL,         button);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "transmit", // TODO: delete this
        "temperature_C",
        "rain_rate_mm_h",
        "button",
        NULL,
};

r_device const bt_rain = {
        .name        = "Biltema rain gauge",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1940,
        .long_width  = 3900,
        .gap_limit   = 4100,
        .reset_limit = 8800,
        .decode_fn   = &bt_rain_decode,
        .disabled    = 1,
        .fields      = output_fields};
