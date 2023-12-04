/** @file
    Remote Grill Thermometer temperature sensor.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int grill_thermometer_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Remote Grill Thermometer -- Generic wireless thermometer with probe.

This is a meat thermometer with no brand / model identification except the FCC ID.

Manufacturer:
- Yangzhou Fupond Electronic Technology Corp., Ltd

Supported Models:
- RF-T0912 (FCC ID TXRFPT0912)

0 - 255 F, frequency 434.052 MHz

Data structure:

10 repetitions of the same 24 bit payload.

    11111111 AAAAAAAA BBBBBBBB

- 1: 8 bit preamble. Always set to 0xff
- A: 8 bit temperature in Fahrenheit. Calculated as (Temp = 255 - A)
- B: Copy of 'A' presumably for message integrity

*/

#include "decoder.h"

static int grill_thermometer_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int raw_value = -1, repeats = 0;

    // use the most recent "valid" data that repeats more than once
    for (int row = 0; row < bitbuffer->num_rows; row++) {
        uint8_t *row_data = bitbuffer->bb[row];

        // validate decode result
        if (bitbuffer->bits_per_row[row] != 24 ||
                row_data[0] != 0xff ||
                row_data[1] != row_data[2]) {
            continue;
        }

        int current_value = row_data[1];

        if (raw_value != current_value) {
            raw_value = current_value;
            repeats   = 0;
        }
        else {
            repeats++;
        }
    }

    if (raw_value == -1 || repeats < 1) {
        return DECODE_ABORT_EARLY;
    }

    int id = 0, temp_f = 0xFF - raw_value;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "RF-T0912 Grill Thermometer",
            "id",               "Id",           DATA_INT,    id,
            "temperature_F",    "Temperature",  DATA_FORMAT, "%i F", DATA_INT, temp_f,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_F",
        NULL,
};

r_device const grill_thermometer = {
        .name        = "RF-T0912 Grill Thermometer",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 252,
        .long_width  = 736,
        .gap_limit   = 5000,
        .reset_limit = 8068,
        .sync_width  = 980,
        .priority    = 10, // lower decode priority due to potential false positives
        .decode_fn   = &grill_thermometer_decode,
        .fields      = output_fields,
};
