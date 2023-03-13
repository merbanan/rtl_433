/** @file
    EMOS E6016 rain gauge.

    Copyright (C) 2022 Dirk Utke-Woehlke <kardinal26@mail.de>
    Copyright (C) 2022 Stefan Tomko <stefan.tomko@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
EMOS E6016 rain gauge.

- Manufacturer: EMOS
- Transmit Interval: every 85s
- Frequency: 433.92 MHz
- Modulation: OOK PWM, INVERTED

Data Layout:

    PP PP PP II BU UU UR RR XX

- P: (24 bit) preamble
- I: (8 bit) ID
- B: (2 bit) battery indication
- U: (18 bit) Unknown
- R: (12 bit) Rain
- X: (8 bit) checksum

Raw data:

    {73} 55 5a 75 cb 13 cf ff ff d6 0

After inversion

         aa a5 8a 34 ec 30 0b b7 29 8

Format string:

    MODEL?:8h8h8h ID?:8h BAT?:2b ?:6h8h4h RAIN:12d CHK:8h 8x

Decoded example:

   MODEL?:aaa58a ID?:34 BAT?:11 ?:2c300 RAIN:2999 CHK:29

*/

static int emos_e6016_rain_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 72);

    if (r < 0) {
        decoder_log(decoder, 2, __func__, "Repeated row fail");
        return DECODE_ABORT_EARLY;
    }
    decoder_logf(decoder, 2, __func__, "Found row: %d", r);

    uint8_t *b = bitbuffer->bb[r];
    // we expect 73 bits
    if (bitbuffer->bits_per_row[r] < 72 || bitbuffer->bits_per_row[r] > 73) {
        decoder_log(decoder, 2, __func__, "Length check fail");
        return DECODE_ABORT_LENGTH;
    }

    // model check 55 5a 75
    if (b[0] != 0x55 || b[1] != 0x5a || b[2] != 0x75) {
        decoder_log(decoder, 2, __func__, "Model check fail");
        return DECODE_ABORT_EARLY;
    }

    bitbuffer_invert(bitbuffer);

    // verify checksum
    if ((add_bytes(b, 8) & 0xff) != b[8]) {
        decoder_log(decoder, 2, __func__, "Checksum fail");
        return DECODE_FAIL_MIC;
    }

    int id        = (b[3]);
    int battery   = (b[4] >> 6);
    int rain_raw  = (b[6] & 0x0f) << 8 | b[7];
    float rain_mm = rain_raw * 0.7f;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "EMOS-E6016R",
            "id",               "House Code",       DATA_INT,    id,
            "battery_ok",       "Battery_OK",       DATA_INT,    !!battery,
            "rain_mm",          "Rain_mm",          DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_mm,
            "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
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
        "rain_mm",
        "mic",
        NULL,
};

r_device const emos_e6016_rain = {
        .name        = "EMOS E6016 rain gauge",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 300,
        .long_width  = 800,
        .gap_limit   = 1000,
        .reset_limit = 2500,
        .decode_fn   = &emos_e6016_rain_decode,
        .fields      = output_fields,
};
