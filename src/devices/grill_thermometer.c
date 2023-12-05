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

9 - 415 F, frequency 434.052 MHz

Data structure:

10 repetitions of the same 24 bit payload.

    AAAAAAAA AAAAAAAA BBBBBBBB

- A: 16 bit temperature in Fahrenheit. Big Endian.
- B: Checksum of A

*/

#include "decoder.h"

static int grill_thermometer_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    short temp_f     = -1;
    int overload     = 0;
    int repeats      = 0;
    uint8_t checksum = 0;

    bitbuffer_invert(bitbuffer);

    // use the most recent "valid" data that repeats more than once
    for (int row = 0; row < bitbuffer->num_rows; row++) {
        uint8_t *row_data = bitbuffer->bb[row];
        checksum          = row_data[0] + row_data[1];

        if (bitbuffer->bits_per_row[row] != 24 ||
                checksum != row_data[2] ||
                checksum == 0) {
            continue;
        }

        short current_value = (row_data[0] << 8) + row_data[1];

        if (temp_f != current_value) {
            temp_f  = current_value;
            repeats = 0;
        }
        else {
            repeats++;
        }
    }

    if (temp_f == -1 || repeats < 1) {
        return DECODE_ABORT_EARLY;
    }

    if (temp_f == -1029) {
        temp_f   = 0;
        overload = 1;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "RF-T0912",
            "temperature_F",    "Temperature",  DATA_FORMAT, "%i F", DATA_INT, temp_f,
            "overload",         "Overload",     DATA_STRING, overload ? "true" : "false",
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "temperature_F",
        "overload",
        "mic",
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
