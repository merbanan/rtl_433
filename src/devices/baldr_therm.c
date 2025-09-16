/** @file
    Baldr Thermo-Hygrometer protocol.

    Copyright (C) 2025 Samuel Holland <samuel@sholland.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/

#include "decoder.h"

/**
Baldr E0666TH Thermo-Hygrometer, which is the remote sensor for the BaldrTherm
B0598T4H4 Solar Thermo-Hygrometer set. There is a channel selection switch (1-3)
inside the battery compartment.

The sensor sends 64 bits 8 times. The packets are ppm modulated (distance
coding) with a pulse of ~500 us followed by a short gap of ~1000 us for a 0 bit
or a long ~2000 us gap for a 1 bit. The sync gap is ~4000 us.

Same modulation as Baldr-Rain, with a format similar to Rubicson-Temperature,
but with more repetitions and no CRC.

Sample data:

    1st device:
      {64}60811bf2c0000800 [CH1, 28.3C, 44%, 3.10V battery]
      {64}60811df380000800 [CH1, 28.5C, 56%, 3.10V battery]
      {64}609124f2d0000800 [CH2, 29.2C, 45%, 3.10V battery]
      {64}609121f2c0000000 [CH2, 28.9C, 44%, 3.10V battery, 13 minutes uptime]
    2nd device:
      {64}86811ef2d000080e [CH1, 28.6C, 45%, 2.78V battery]
      {64}868120f2c000080e [CH1, 28.8C, 44%, 3.10V battery]
      {64}860121f2c000080e [CH1, 28.9C, 44%, 2.51V battery]
    3rd device:
      {64}9c211af2d0000812 [CH3, 28.2C, 45%, 2.50V battery]
      {64}9ca11df2e0000812 [CH3, 28.5C, 46%, 2.65V battery]

The data is grouped in 16 nibbles:

    II FT TT fH H0 00 0S JJ

- I : 8 bit ID, persistent after battery changes
- F : 4 bit flags (battery ok, unused, channel number x2)
- T : 12 bit temperature value (Celsius * 10)
- f : always 0xf
- H : 8 bit humidity value (percent)
- 0 : always 0x0000
- S : 4 bit flags (startup indicator, unused x3)
- J : 8 bit ID, persistent after battery changes

The startup indicator transitions from 1 to 0 after 10-15 minutes.

*/
static int baldr_therm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int r = bitbuffer_find_repeated_row(bitbuffer, 8, 64);
    if (r < 0) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *b = bitbuffer->bb[r];

    // we expect 64 bits but there might be a trailing 0 bit
    if (bitbuffer->bits_per_row[r] > 65) {
        return DECODE_ABORT_LENGTH;
    }

    // Reduce false positives.
    if ((b[1] & 0x40) != 0x00 || (b[3] & 0xf0) != 0xf0 ||
            (b[4] & 0x0f) != 0x00 || b[5] != 0x00 || (b[6] & 0xf7) != 0x00) {
        return DECODE_ABORT_EARLY;
    }

    int id       = (b[0] << 8) | b[7];
    int battery  = (b[1] & 0x80);
    int channel  = ((b[1] & 0x30) >> 4) + 1;
    int temp_raw = (int16_t)((b[1] << 12) | (b[2] << 4)); // sign-extend
    float temp_c = (temp_raw >> 4) * 0.1f;
    int humidity = (uint8_t)((b[3] << 4) | (b[4] >> 4));
    int startup  = (b[6] & 0x08);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Baldr-E0666TH",
            "id",               "ID",           DATA_INT,    id,
            "channel",          "Channel",      DATA_INT,    channel,
            "battery_ok",       "Battery",      DATA_INT,    !!battery,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "startup",          "Startup",      DATA_INT,    !!startup,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "startup",
        NULL,
};

r_device const baldr_therm = {
        .name        = "Baldr E0666TH Thermo-Hygrometer",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .gap_limit   = 3000,
        .reset_limit = 5000,
        .decode_fn   = &baldr_therm_decode,
        .fields      = output_fields,
};
