/** @file
    Eurochron temperature and humidity sensor.

    Copyright (c) 2019 by Oliver Weyhmüller

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Eurochron temperature and humidity sensor.

Datagram format:

    IIIIIIII B00P0000 HHHHHHHH TTTTTTTT TTTT

- I: ID (new ID will be generated at battery change!)
- B: Battery low
- P: TX-Button pressed
- H: Humidity (%)
- T: Temperature (°C10)
- 0: Unknown / always zero

Device type identification is only possible by datagram length
and some zero bits. Therefore this device is disabled
by default (as it could easily trigger false alarms).

Observed update intervals:
- transmission time slot every 12 seconds
- at least once within 120 seconds (with stable values)
- down to 12 seconds (with rapidly changing values)
*/

#include "decoder.h"

static int eurochron_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int row;
    uint8_t *b;
    int temp_raw, humidity, device, battery_low, button;
    float temp_c;

    /* Validation checks */
    row = bitbuffer_find_repeated_row(bitbuffer, 3, 36);

    if (row < 0) // repeated rows?
        return 0;

    if (bitbuffer->bits_per_row[row] > 36) // 36 bits per row?
        return 0;

    b = bitbuffer->bb[row];

    if (b[1] & 0x0F) // is lower nibble of second byte zero?
        return 0;

    /* Extract data */
    device = b[0];

    temp_raw = (int16_t)((b[3] << 8) | (b[4] & 0xf0)) >> 4;
    temp_c  = (float)temp_raw * 0.1;

    humidity = b[2];

    battery_low = b[1] >> 7;

    button = (b[1] & 0x10) >> 4;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Eurochron-TH",
            "id",               "",             DATA_INT,    device,
            "battery_ok",       "Battery",      DATA_INT,    !battery_low,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_INT,    humidity,
            "button",           "Button",       DATA_INT,    button,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "humidity",
        "button",
        NULL,
};

r_device eurochron = {
        .name          = "Eurochron temperature and humidity sensor",
        .modulation    = OOK_PULSE_PPM,
        .short_width   = 1016,
        .long_width    = 2024,
        .gap_limit     = 2100,
        .reset_limit   = 8200,
        .decode_fn     = &eurochron_decode,
        .disabled      = 1,
        .fields        = output_fields,
};
