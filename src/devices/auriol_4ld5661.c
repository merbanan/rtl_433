/** @file
    Auriol 4-LD5661 sensor.

    Copyright (C) 2021 Balazs H.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Lidl Auriol 4-LD5661 sensor.

See also issue #1857.

Data layout:

    II B TTT F RRRRRR

- I: id, 8 bit: what we've seen so far are 1a, c6
- B: battery, 4 bit: 0x8 if normal, 0x0 if low
- T: temperature, 12 bit: 2's complement, scaled by 10
- F: 4 bit: seems to be 0xf constantly, a separator between temp and rain
- R: rain sensor, probably the remaining 24 bit: a counter for every 0.3mm of rain

*/

#include "decoder.h"

static int auriol_4ld5661_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = 0;

    for (int i = 0; i < bitbuffer->num_rows; i++) {
        if (bitbuffer->bits_per_row[i] != 52) {
            ret = DECODE_ABORT_LENGTH;
            continue;
        }

        uint8_t *b  = bitbuffer->bb[i];
        int id      = b[0];
        int batt_ok = b[1] >> 7;

        if (b[3] != 0xf0 || (b[1] & 0x70) != 0) {
            ret = DECODE_FAIL_MIC;
            continue;
        }

        int temp_raw = (int16_t)(((b[1] & 0x0f) << 12) | (b[2] << 4)); // uses sign extend
        float temp_c = (temp_raw >> 4) * 0.1F;

        int rain_raw = (b[4] << 12) | (b[5] << 4) | b[6] >> 4;
        float rain   = rain_raw * 0.3F;

        /* clang-format off */
        data_t *data = data_make(
                "model",            "Model",        DATA_STRING, "Auriol-4LD5661",
                "id",               "ID",           DATA_FORMAT, "%02x", DATA_INT, id,
                "battery_ok",       "Battery OK",   DATA_INT, batt_ok,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
                "rain_mm",          "Rain",         DATA_FORMAT, "%.01f mm", DATA_DOUBLE, rain,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return ret;
}

static char *output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "rain_mm",
        NULL,
};

r_device auriol_4ld5661 = {
        .name        = "Auriol 4-LD5661 temperature/rain sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .sync_width  = 2500,
        .gap_limit   = 2500,
        .reset_limit = 4000,
        .decode_fn   = &auriol_4ld5661_decode,
        .disabled    = 1, // no sync-word, no fix id, no checksum
        .fields      = output_fields,
};
