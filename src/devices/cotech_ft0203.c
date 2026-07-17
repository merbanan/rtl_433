/** @file
    Cotech FT0203 / 18-3676 anemometer.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Cotech FT0203 / 18-3676 anemometer.

OOK modulated with Manchester encoding. 72 bit payload, byte layout:

    SYNC:8h ID:11d BATT:1b ?:1b DIR_MSB:1b GUST_MSB:1b AVG_MSB:1b AVG:8d GUST:8d DIR:8d CONST:16h CRC:8h

- SYNC: always 0x14
- ID: random id, changes on battery reset
- BATT: 1 = battery ok, 0 = low (opposite polarity of the related Cotech-367959)
- DIR_MSB/DIR: wind direction in degrees, (DIR_MSB<<8)|DIR
- GUST_MSB/GUST: wind gust, scaled by 10 -- MSB bit unconfirmed, never seen set
- AVG_MSB/AVG: average wind, scaled by 10 -- MSB bit unconfirmed, never seen set
- CONST: always 0xffff
- CRC: CRC-8, poly 0x31, init 0xc0

Reverse-engineered collaboratively in issue #2569 by zuckschwerdt, klohner,
ProfBoc75 and GreatAlbatross from hand-transcribed hex codes (no working
decoder was ever committed). CRC and direction are confirmed against 50
codes from that thread; avg/gust confirmed against all but one, resolving
a labeling mix-up in the original thread. No real IQ capture was ever
posted, so this ships disabled until verified against one.
*/

#define COTECH_FT0203_NUM_BYTES 9

static int cotech_ft0203_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        int row_bits = bitbuffer->bits_per_row[row];
        uint8_t b[COTECH_FT0203_NUM_BYTES];

        for (int pos = 0; pos + COTECH_FT0203_NUM_BYTES * 8 <= row_bits; ++pos) {
            bitbuffer_extract_bytes(bitbuffer, row, pos, b, COTECH_FT0203_NUM_BYTES * 8);

            if (b[0] != 0x14 || b[6] != 0xff || b[7] != 0xff)
                continue;
            if (crc8(b, COTECH_FT0203_NUM_BYTES, 0x31, 0xc0) != 0)
                continue;

            int id         = (b[1] << 3) | (b[2] >> 5);
            int battery_ok = (b[2] >> 4) & 0x1;
            int dir_msb    = (b[2] >> 2) & 0x1;
            int gust_msb   = (b[2] >> 1) & 0x1;
            int avg_msb    = b[2] & 0x1;
            int avg_raw    = (avg_msb << 8) | b[3];
            int gust_raw   = (gust_msb << 8) | b[4];
            int dir_deg    = (dir_msb << 8) | b[5];

            /* clang-format off */
            data_t *data = data_make(
                    "model",         "",             DATA_STRING, "Cotech-FT0203",
                    "id",            "ID",           DATA_INT,    id,
                    "battery_ok",    "Battery",      DATA_INT,    battery_ok,
                    "wind_dir_deg",  "Wind direction", DATA_INT,  dir_deg,
                    "wind_avg_m_s",  "Wind",         DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, avg_raw * 0.1,
                    "wind_max_m_s",  "Gust",         DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, gust_raw * 0.1,
                    "mic",           "Integrity",    DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            return 1;
        }
    }

    return DECODE_FAIL_SANITY;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "wind_dir_deg",
        "wind_avg_m_s",
        "wind_max_m_s",
        "mic",
        NULL,
};

r_device const cotech_ft0203 = {
        .name        = "Cotech FT0203/18-3676 anemometer",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 500,
        .long_width  = 0,
        .reset_limit = 1200,
        .decode_fn   = &cotech_ft0203_decode,
        .fields      = output_fields,
        .disabled    = 1, // no real IQ capture posted in issue #2569, needs field verification
};
