/** @file
    Auriol 4-LD5661 and 4-LD6313 sensors.

    Copyright (C) 2021 Balazs H.
    Copyright (C) 2023 Peter Soos

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Lidl Auriol 4-LD5661 / 4-LD6313 sensor.

See also issue #1857 and PR #2633

Data layout:

    II B TTT F RRRRRR

- I: id, 8 bit: what we've seen so far are 1a, c6 for 4-LD5661 and 60 for 4LD6313
- B: battery, 4 bit: 0x8 if normal, 0x0 if low
- T: temperature, 12 bit: 2's complement, scaled by 10
- F: 4 bit: seems to be 0xf constantly, a separator between temp and rain
- R: rain sensor, probably the remaining 24 bit: a counter for every 0.3 mm (4-LD5661) or 0.242 mm (4-LD6313)

*/

#include "decoder.h"

static int auriol_4ld_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    const char *model_4ds5661 = "Auriol-4LD5661";
    const char *model_4ds6313 = "Auriol-4LD6313";
    const char *model_unknown = "Unknown model";
    char model[16];
    
    int ret = 0;

    for (int i = 0; i < bitbuffer->num_rows; i++) {
        if (bitbuffer->bits_per_row[i] != 52) {
            ret = DECODE_ABORT_LENGTH;
            continue;
        }

        uint8_t *b  = bitbuffer->bb[i];
        int id      = b[0];
        int batt_ok = b[1] >> 7;

	switch (id) {
        case 0x1a:
        case 0xc6:
            strncpy (model, model_4ds5661, strlen(model_4ds5661)+1);
            break;
        case 0x60:
            strncpy (model, model_4ds6313, strlen(model_4ds6313)+1);
            break;
        default:
            strncpy (model, model_unknown, strlen(model_unknown)+1);
	}

        if (b[3] != 0xf0 || (b[1] & 0x70) != 0) {
            ret = DECODE_FAIL_MIC;
            continue;
        }

        int temp_raw = (int16_t)(((b[1] & 0x0f) << 12) | (b[2] << 4)); // uses sign extend
        float temp_c = (temp_raw >> 4) * 0.1F;

        int rain_raw = (b[4] << 12) | (b[5] << 4) | b[6] >> 4;

        /* The display unit which comes with this devices, multiplies gauge tip counts by 0.3 mm, which seems
           to be very inaccurate. We did a lot of measurements, the gauge's capacity is about 7.5 ml, the
           rain collection surface diameter is 96mm, 7.5 ml /((9.6 cm/2)^2*pi) ~= 1 mm of rain. Therefore
           we decided to correct this multiplier.
i          The rain bucket tips at 7.2 ml for 4-LS6313. The main unit counts 0.242 mm per sensor tips.
	   The physical parameters are same. The calculation
	   and the result is similar: 7.2 ml / ((96 mm / 2)^2 * pi) ~= 1 mm (more exactly 0.995 mm)
           See also:
               https://github.com/merbanan/rtl_433/issues/1837
               https://github.com/merbanan/rtl_433/pull/2633
        */
        float rain   = rain_raw * 1.0F;

        /* clang-format off */
        data_t *data = data_make(
                "model",            "Model",        DATA_STRING, model,
                "id",               "ID",           DATA_FORMAT, "%02x", DATA_INT, id,
                "battery_ok",       "Battery OK",   DATA_INT, batt_ok,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
                "rain_mm",          "Rain",         DATA_FORMAT, "%.01f mm", DATA_DOUBLE, rain,
                "rain",             "Rain tips",    DATA_INT, rain_raw,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return ret;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "rain_mm",
        NULL,
};

r_device const auriol_4ld = {
        .name        = "Auriol 4-LD5661/4-LD6313 temperature/rain sensors",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .sync_width  = 2500,
        .gap_limit   = 2500,
        .reset_limit = 4000,
        .decode_fn   = &auriol_4ld_decode,
        .disabled    = 1, // no sync-word, no fix id, no checksum
        .fields      = output_fields,
};
