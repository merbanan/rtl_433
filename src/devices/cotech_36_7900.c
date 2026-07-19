/** @file
    Cotech 36-7900 rain gauge.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Cotech 36-7900 rain gauge, 433 MHz.

OOK PPM, short 1000 us, long 2000 us, packet gap ~3500-3900 us. Each
transmission repeats the same 60 bit row ~13 times; see issue #3537.

No computed CRC/checksum has been found. There is however a 24 bit
span that is exactly zero in all 66 unique real messages seen across
5 days and the sensor's full observed temperature/rain range -- likely
reserved bits rather than a coincidence, but functionally just as good
as a checksum for rejecting noise, so it's checked as one. Combined
with the 16 bit fixed marker and `bitbuffer_find_repeated_row()`'s
redundancy requirement, false positives should be rare in practice.

Data layout, 60 bits:

    MMMMMMMMMMMMMMMM TTTTTTTTTTTT 000000000000000000000000 RRRRRRRRRRRR

- M: 16 bit fixed marker, `0xab80` in every capture seen so far (a
  single physical sensor, never reset -- could double as a per-device
  ID, not independently confirmed since no second unit has been tested).
- T: 12 bit signed temperature, scaled by 10 (raw/10 = degC). Confirmed
  against a full day's smooth diurnal temperature curve.
- 0: 24 bits, always zero in every real message seen (see above).
- R: 12 bit rain counter, monotonically increasing, confirmed against a
  controlled test where each bucket tip increased it by ~39-42 (some
  tip-to-tip variance is expected from bucket mechanics). The mm-per-count
  scale is not confirmed -- no absolute water-volume calibration was
  reported -- so the raw count is exposed unscaled rather than guessed at.
*/

static int cotech_36_7900_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 8, 60);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[row] != 60) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[8] = {0};
    bitbuffer_extract_bytes(bitbuffer, row, 0, b, 60);

    if (b[0] != 0xab || (b[1] >> 4) != 0x8) {
        return DECODE_ABORT_EARLY;
    }

    if (b[3] != 0x00 || b[4] != 0x00 || b[5] != 0x00) {
        return DECODE_FAIL_MIC;
    }

    int id = (b[0] << 8) | b[1];

    int temp_raw = ((b[1] & 0x0f) << 8) | b[2];
    if (temp_raw & 0x800) {
        temp_raw -= 0x1000;
    }
    float temperature_c = temp_raw * 0.1f;

    int rain_raw = (b[6] << 4) | (b[7] >> 4);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Cotech-367900",
            "id",               "ID",           DATA_FORMAT, "%04x", DATA_INT, id,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)temperature_c,
            "rain_raw",         "Rain",         DATA_INT,    rain_raw,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "rain_raw",
        NULL,
};

r_device const cotech_36_7900 = {
        .name        = "Cotech 36-7900 rain gauge",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .gap_limit   = 3500,
        .reset_limit = 4500,
        .decode_fn   = &cotech_36_7900_decode,
        .disabled    = 1, // no computed checksum, only a reserved-bits plausibility check
        .fields      = output_fields,
};
