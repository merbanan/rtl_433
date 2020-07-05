/** @file
    Ambient Weather WH31E, EcoWitt WH40 protocol.

    Copyright (C) 2018 Christian W. Zuckschwerdt <zany@triq.net>
    based on protocol analysis by James Cuff and Michele Clamp,
    EcoWitt WH40 analysis by Helmut Bachmann,
    Ecowitt WS68 analysis by Tolip Wen.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Ambient Weather WH31E protocol.
915 MHz FSK PCM Thermo-Hygrometer Sensor (bundled as Ambient Weather WS-3000-X5).

Note that Ambient Weather and EcoWitt are likely rebranded Fine Offset products.

56 us bit length with a warm-up of 1336 us mark(pulse), 1996 us space(gap),
a preamble of 48 bit flips (0xaaaaaaaaaaaa) and a 0x2dd4 sync-word.

Data layout:

    YY II CT TT HH XX AA ?? ?? ?? ??

- Y is a fixed Type Code of 0x30
- I is a device ID
- C is the Channel number (only the lower 3 bits)
- T is 12bits Temperature in C, scaled by 10, offset 400
- H is Humidity
- X is CRC-8, poly 0x31, init 0x00
- A is SUM-8

Example packets:

    {177} aa aa aa aa aa aa  2d d4  30 c3 8 20a 5e  df bc   07 56 a7 ae  00 00 00 00
    {178} aa aa aa aa aa aa  2d d4  30 44 9 21a 39  5a b3   07 45 04 5f  00 00 00 00

Some payloads:

    30 c3 81 d5 5c 2a cf 08 35 44 2c
    30 35 c2 2f 3c 0f a1 07 52 29 9f
    30 35 c2 2e 3c fb 8c 07 52 29 9f
    30 c9 a2 1e 40 0c 05 07 34 c6 b1
    30 2b b2 14 3d 94 f2 08 53 78 e6
    30 c9 a2 1f 40 f8 f2 07 34 c6 b1
    30 44 92 13 3e 0e 65 07 45 04 5f
    30 44 92 15 3d 07 5f 07 45 04 5f
    30 c3 81 d6 5b 90 35 08 35 44 2c

EcoWitt WH40 protocol.
Seems to be the same as Fine Offset WH5360 or Ecowitt WH5360B.

Data layout:

    YY 00 IIII FF RRRR XX AA 00 02 ?? 00 00

- Y is a fixed Type Code of 0x40
- I is a device ID
- F is perhaps flags, but only seen fixed 0x10 so far
- R is the rain bucket tip count, 0.1mm increments
- X is CRC-8, poly 0x31, init 0x00
- A is SUM-8

Some payloads:

    4000 cd6f 10 0000  64 f0 ; 00 027b 0000
    4000 cd6f 10 0001  55 e2 ; 00 02f6 0000
    4000 cd6f 10 0002  06 94 ; 00 02ed 0000
    4000 cd6f 10 0003  37 c6 ; 00 02db 0000
    4000 cd6f 10 0004  a0 30 ; 00 02b7 0000
    4000 cd6f 10 0005  91 22 ; 00 02de 0000
    4000 cd6f 10 0006  c2 54 ; 00 02bd 0000
    4000 cd6f 10 0007  f3 86 ; 00 027b 0000
    4000 cd6f 10 0008  dd 71 ; 00 02f6 0000
    4000 cd6f 10 0009  ec 81 ; 00 02ed 0000
    4000 cd6f 10 000a  bf 55 ; 00 02db 0000

Samples with 1.2V battery (last 2 samples contain 1 manual bucket tip)

    4000 cd6f 10 0000  64 f0 ; 00 01de 00b0
    4000 cd6f 10 0000  64 f0 ; 00 02de 00b0
    4000 cd6f 10 0000  64 f0 ; 00 02bd 0000
    4000 cd6f 10 0001  55 e2 ; 00 027b 0000
    4000 cd6f 10 0001  55 e2 ; 00 027b 0000

Samples with 0.9V battery (last 3 samples contain 1 manual bucket tip)

    4000 cd6f 10 0000  64 f0 ; 00 16de 0000
    4000 cd6f 10 0000  64 f0 ; 00 02de 0000
    4000 cd6f 10 0001  55 e2 ; 00 02bd 0000
    4000 cd6f 10 0001  55 e2 ; 00 027b 0000
    4000 cd6f 10 0001  55 e2 ; 00 027b 0000

Ecowitt WS68 Anemometer protocol.

Data layout:

    TYPE:8h ?8h ID:16h LUX:16h BATT:8h WDIR_H:4h 4h8h8h WSPEED:8h WDIR_LO:8h WGUST:8h ?8h CRC:8h SUM:8h ?8h4h

Some payloads:

    68 0000 c5 0000 4b 0f ffff 00 5a 00 00 d0af 104
    68 0000 c5 0000 4b 0f ffff 00 b4 00 00 79b2 102
    68 0000 c5 0000 4b 0f ffff 7e e0 94 00 75ec 102
    68 0000 c5 0000 4b 2f ffff 00 0e 00 00 8033 208
    68 0000 c5 000f 4b 0f ffff 00 2e 00 00 d395 108
    68 0000 c5 0107 4b 0f ffff 00 2e 00 02 a663 100

*/

#include "decoder.h"

static int ambientweather_whx_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int events = 0;
    uint8_t b[18]; // actually only 6/9/17.5 bytes, no indication what the last 5 might be
    int row;
    int msg_type;
    int id, channel, battery_ok, temp_raw;
    int humidity, rain_raw;
    float temp_c;
    char extra[11];

    uint8_t const preamble[] = {0xaa, 0x2d, 0xd4}; // (partial) preamble and sync word

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        // Validate message and reject it as fast as possible : check for preamble
        unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, 24);
        // no preamble detected, move to the next row
        if (start_pos == bitbuffer->bits_per_row[row])
            continue; // DECODE_ABORT_EARLY
        if (decoder->verbose)
            fprintf(stderr, "%s: WH31E/WH40 detected, buffer is %d bits length\n", __func__, bitbuffer->bits_per_row[row]);

        // remove preamble, keep whole payload
        bitbuffer_extract_bytes(bitbuffer, row, start_pos + 24, b, 18 * 8);

        if (b[0] == 0x30) {
            // WH31E
            uint8_t c_crc = crc8(b, 6, 0x31, 0x00);
            if (c_crc) {
                if (decoder->verbose)
                    fprintf(stderr, "%s: WH31E bad CRC\n", __func__);
                continue; // DECODE_FAIL_MIC
            }
            uint8_t c_sum = add_bytes(b, 6) - b[6];
            if (c_sum) {
                if (decoder->verbose)
                    fprintf(stderr, "%s: WH31E bad SUM\n", __func__);
                continue; // DECODE_FAIL_MIC
            }

            msg_type   = b[0]; // fixed 0x30
            id         = b[1];
            battery_ok = (b[2] >> 7);
            channel    = ((b[2] & 0x70) >> 4) + 1;
            temp_raw   = ((b[2] & 0x0f) << 8) | b[3];
            temp_c     = temp_raw * 0.1 - 40.0;
            humidity   = b[4];
            sprintf(extra, "%02x%02x%02x%02x%02x", b[6], b[7], b[8], b[9], b[10]);

            /* clang-format off */
            data = data_make(
                    "model",            "",             DATA_STRING, "AmbientWeather-WH31E",
                    "id" ,              "",             DATA_INT,    id,
                    "channel",          "Channel",      DATA_INT,    channel,
                    "battery",          "Battery",      DATA_STRING, battery_ok ? "OK" : "LOW",
                    "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                    "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
                    "data",             "Extra Data",   DATA_STRING, extra,
                    "mic",              "Integrity",    DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            events++;
        }

        else if (b[0] == 0x40) {
            // WH40
            uint8_t c_crc = crc8(b, 8, 0x31, 0x00);
            if (c_crc) {
                if (decoder->verbose)
                    fprintf(stderr, "%s: WH40 bad CRC\n", __func__);
                continue; // DECODE_FAIL_MIC
            }
            uint8_t c_sum = add_bytes(b, 8) - b[8];
            if (c_sum) {
                if (decoder->verbose)
                    fprintf(stderr, "%s: WH40 bad SUM\n", __func__);
                continue; // DECODE_FAIL_MIC
            }

            msg_type   = b[0]; // fixed 0x40
            id         = (b[2] << 8) | b[3];
            battery_ok = (b[4] >> 7);
            channel    = ((b[4] & 0x70) >> 4) + 1;
            rain_raw   = (b[5] << 8) | b[6];
            sprintf(extra, "%02x%02x%02x%02x%02x", b[9], b[10], b[11], b[12], b[13]);

            /* clang-format off */
            data = data_make(
                    "model",            "",             DATA_STRING, "EcoWitt-WH40",
                    "id" ,              "",             DATA_INT,    id,
                    //"channel",          "Channel",      DATA_INT,    channel,
                    //"battery",          "Battery",      DATA_STRING, battery_ok ? "OK" : "LOW",
                    "rain_mm",          "Total Rain",   DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_raw * 0.1,
                    "data",             "Extra Data",   DATA_STRING, extra,
                    "mic",              "Integrity",    DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            events++;
        }

        else if (b[0] == 0x68) {
            // WS68
            uint8_t c_crc = crc8(b, 15, 0x31, 0x00);
            if (c_crc) {
                if (decoder->verbose)
                    fprintf(stderr, "%s: WH68 bad CRC\n", __func__);
                continue; // DECODE_FAIL_MIC
            }
            uint8_t c_sum = add_bytes(b, 15) - b[15];
            if (c_sum) {
                if (decoder->verbose)
                    fprintf(stderr, "%s: WH68 bad SUM\n", __func__);
                continue; // DECODE_FAIL_MIC
            }

            msg_type   = b[0]; // fixed 0x68
            id         = (b[2] << 8) | b[3];
            int lux    = (b[4] << 8) | b[5];
            int batt   = b[6];
            battery_ok = batt > 0x30; // wild guess
            int wspeed = b[10];
            int wgust  = b[12];
            int wdir   = ((b[7] & 0x20) >> 5) | b[11];
            sprintf(extra, "%02x %02x%01x", b[13], b[16], b[17] >> 4);

            /* clang-format off */
            data = data_make(
                    "model",            "",             DATA_STRING, "EcoWitt-WS68",
                    "id" ,              "",             DATA_INT,    id,
                    "battery_raw",      "Battery Raw",  DATA_INT,    batt,
                    "battery",          "Battery",      DATA_STRING, battery_ok ? "OK" : "LOW",
                    "lux_raw",          "lux",          DATA_INT,    lux,
                    "wind_avg_raw",     "Wind Speed",   DATA_INT,    wspeed,
                    "wind_max_raw",     "Wind Gust",    DATA_INT,    wgust,
                    "wind_dir_deg",     "Wind dir",     DATA_INT,    wdir,
                    "data",             "Extra Data",   DATA_STRING, extra,
                    "mic",              "Integrity",    DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            events++;
        }

        else {
            if (decoder->verbose)
                fprintf(stderr, "%s: unknown message type %02x (expected 0x30/0x40/0x68)\n", __func__, b[0]);
        }
    }
    return events;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "battery",
        "temperature_C",
        "humidity",
        "rain_mm",
        "lux",
        "wind_avg_km_h",
        "wind_max_km_h",
        "wind_dir_deg",
        "data",
        "mic",
        NULL,
};

r_device ambientweather_wh31e = {
        .name        = "Ambient Weather WH31E Thermo-Hygrometer Sensor, EcoWitt WH40 rain gauge",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 56,
        .long_width  = 56,
        .reset_limit = 1500,
        .gap_limit   = 1800,
        .decode_fn   = &ambientweather_whx_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
