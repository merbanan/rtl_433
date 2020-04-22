/** @file
    WS2032 weather station.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
 */
/**
WS2032 weather station.

- Outdoor temperature range: -40F to 140F (-40C to 60C)
- Temperature accuracy: +- 1.0 C
- Humidity range: 20% to 90%
- Humidity accuracy: +-5%
- Wind direction: E,S,W,N,SE,NE,SW,NW
- Wind direction accuracy: +- 10 deg
- Wind speed: 0 to 50m/s, Accuracy: 0.1 m/s

Data format:

    1x PRE:8h ID:16h ?8h DIR:4h TEMP:12d HUM:8d AVG?8d GUST?8d 24h SUM8h CHK8h TRAIL:3b

OOK with PWM. Long = 1000 us, short = 532 us, gap = 484 us.
The overlong and very short pulses are sync, see the Pulseview.

Temp, not 2's complement but a dedicated sign-bit, i.e. 1 bit sign, 11 bit temp.

*/

#include "decoder.h"

static int fineoffset_ws2032_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x0a}; // 8 bits, 0xf5 inverted

    data_t *data;
    uint8_t b[14];

    // find a proper row
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 14 * 8); // expected: 3 rows of 113 bits
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    unsigned offset = bitbuffer_search(bitbuffer, row, 0, preamble, 8);
    if (offset + 14 * 8 > bitbuffer->bits_per_row[row]) {
        return DECODE_ABORT_LENGTH;
    }

    // invert and align the row
    bitbuffer_invert(bitbuffer);
    bitbuffer_extract_bytes(bitbuffer, row, offset, b, 14 * 8);

    // verify the checksums
    int sum = add_bytes(b, 12);
    if (sum == 0) {
        return DECODE_FAIL_SANITY; // discard all zeros
    }
    if ((sum & 0xff) != b[12]) {
        return DECODE_FAIL_MIC; // sum mismatch
    }
    if (crc8(b, 14, 0x31, 0x00)) {
        return DECODE_FAIL_MIC; // crc mismatch
    }

    // get weather sensor data
    // 1x PRE:8h ID:16h ?8h DIR:4h TEMP:12d HUM:8d AVG?8d GUST?8d 24h SUM8h CHK8h TRAIL:3b
    int device_id     = (b[1] << 8) | (b[2]);
    int flags         = (b[3]);
    //int battery_low   = b[3] & 0x80;
    float dir         = (b[4] >> 4) * 22.5f;
    int temp_sign     = (b[4] & 0x08) ? -1 : 1;
    int temp_raw      = ((b[4] & 0x07) << 8) | b[5];
    float temperature = temp_sign * temp_raw * 0.1;
    int humidity      = b[6];
    float speed       = (b[7] * 0.43f) * 3.6f; // m/s -> km/h
    float gust        = (b[8] * 0.43f) * 3.6f; // m/s -> km/h
    int rain_raw      = (b[9] << 16) | (b[10] << 8) | b[11]; // maybe?

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "WS2032",
            "id",               "StationID",        DATA_FORMAT, "%04X",    DATA_INT,    device_id,
            //"battery_ok",       "Battery",          DATA_INT,    !battery_low,
            "temperature_C",    "Temperature",      DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
            "humidity",         "Humidity",         DATA_FORMAT, "%u %%",   DATA_INT,    humidity,
            _X("wind_dir_deg","direction_deg"),     "Wind Direction",    DATA_FORMAT, "%.01f",   DATA_DOUBLE, dir,
            "wind_avg_km_h",    "Wind avg speed",   DATA_FORMAT, "%.01f",   DATA_DOUBLE, speed,
            "wind_max_km_h",    "Wind gust",        DATA_FORMAT, "%.01f",   DATA_DOUBLE, gust,
            "maybe_flags",      "Flags?",           DATA_FORMAT, "%02x",    DATA_INT,    flags,
            "maybe_rain",       "Rain?",            DATA_FORMAT, "%06x",    DATA_INT,    rain_raw,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "battery",
        "temperature_C",
        "humidity",
        "direction_deg", // TODO: remove this
        "wind_dir_deg",
        "wind_avg_km_h",
        "wind_max_km_h",
        NULL,
};

r_device ws2032 = {
        .name        = "WS2032 weather station",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 500,
        .long_width  = 1000,
        .gap_limit   = 750,
        .reset_limit = 4000,
        .decode_fn   = &fineoffset_ws2032_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
