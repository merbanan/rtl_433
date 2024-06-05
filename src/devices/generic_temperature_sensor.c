/** @file
    Generic temperature sensor 1.

    Copyright (C) 2015 Alexandre Coffignal

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Generic temperature sensor 1.

10 24 bits frames:

    IIIIIIII BBTTTTTT TTTTTTTT

- I: 8 bit ID
- B: 2 bit? Battery ?
- T: 12 bit Temp
*/

#include "decoder.h"

static int generic_temperature_sensor_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[1];
    int i, device, battery, temp_raw;
    float temp_f;

    for (i = 1; i < 10; i++) {
        if (bitbuffer->bits_per_row[i] != 24) {
            /*10 24 bits frame*/
            return DECODE_ABORT_LENGTH;
        }
    }

    // reduce false positives
    if ((b[0] == 0 && b[1] == 0 && b[2] == 0)
            || (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff)) {
        return DECODE_ABORT_EARLY;
    }

    device  = (b[0]);
    battery = (b[1] & 0xC0) >> 6;
    temp_raw = (int16_t)(((b[1] & 0x3f) << 10) | (b[2] << 2));
    temp_f  = (temp_raw >> 4) * 0.1f;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING,    "Generic-Temperature",
            "id",               "Id",           DATA_INT,       device,
            "battery_ok",       "Battery?",     DATA_INT,       battery,
            "temperature_C",    "Temperature",  DATA_FORMAT,    "%.2f C",  DATA_DOUBLE,    temp_f,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        NULL,
};

r_device const generic_temperature_sensor = {
        .name        = "Generic temperature sensor 1",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .gap_limit   = 4800,
        .reset_limit = 10000,
        .decode_fn   = &generic_temperature_sensor_callback,
        .fields      = output_fields,
};
