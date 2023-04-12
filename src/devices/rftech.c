/** @file
    RF-tech decoder (INFRA 217S34).

    Copyright (C) 2016 Erik Johannessen

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
RF-tech decoder (INFRA 217S34).

Also marked INFRA 217S34
Ewig Industries Macao

Example of message:

    01001001 00011010 00000100

- First byte is unknown, but probably id.
- Second byte is the integer part of the temperature.
- Third byte bits 0-3 is the fraction/tenths of the temperature.
- Third byte bit 7 is 1 with fresh batteries.
- Third byte bit 6 is 1 on button press.

More sample messages:

    {24} ad 18 09 : 10101101 00011000 00001001
    {24} 3e 17 09 : 00111110 00010111 00001001
    {24} 70 17 03 : 01110000 00010111 00000011
    {24} 09 17 01 : 00001001 00010111 00000001

With fresh batteries and button pressed:

    {24} c5 16 c5 : 11000101 00010110 11000101

*/

#include "decoder.h"

static int rftech_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 24);

    if (r < 0 || bitbuffer->bits_per_row[r] != 24)
        return DECODE_ABORT_LENGTH;
    uint8_t *b = bitbuffer->bb[r];

    int sensor_id = b[0];
    float temp_c  = (b[1] & 0x7f) + (b[2] & 0x0f) * 0.1f;
    if (b[1] & 0x80)
        temp_c = -temp_c;

    int battery = (b[2] & 0x80) == 0x80;
    int button  = (b[2] & 0x60) != 0;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "RF-tech",
            "id",               "Id",           DATA_INT,    sensor_id,
            "battery_ok",       "Battery",      DATA_INT,    battery,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "button",           "Button",       DATA_INT,    button,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const csv_output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "button",
        NULL,
};

r_device const rftech = {
        .name        = "RF-tech",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .gap_limit   = 5000,
        .reset_limit = 10000,
        .decode_fn   = &rftech_callback,
        .disabled    = 1,
        .fields      = csv_output_fields,
};
