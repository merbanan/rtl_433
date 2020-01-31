/** @file
    Ambient Weather WH31E, EcoWitt WH40 protocol.

    Copyright (C) 2018 Christian W. Zuckschwerdt <zany@triq.net>
    based on protocol analysis by James Cuff and Michele Clamp,
    EcoWitt WH40 analysis by Helmut Bachmann.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Ambient Weather WH31E protocol.
915 MHz FSK PCM Thermo-Hygrometer Sensor (bundled as Ambient Weather WS-3000-X5).

56 us bit length with a warm-up of 1336 us mark(pulse), 1996 us space(gap),
a preamble of 48 bit flips (0xaaaaaaaaaaaa) and a 0x2dd4 sync-word.

Data layout:

    YY II CT TT HH XX ?? ?? ?? ?? ??

- Y is a fixed Type Code of 0x30
- I is a device ID
- C is the Channel number (only the lower 3 bits)
- T is 12bits Temperature in C, scaled by 10, offset 400
- H is Humidity
- X is CRC-8, poly 0x31, init 0x00

Example packets:

    {177} aa aa aa aa aa aa  2d d4  30 c3 8 20a 5e  df   bc 07 56 a7 ae  00 00 00 00
    {178} aa aa aa aa aa aa  2d d4  30 44 9 21a 39  5a   b3 07 45 04 5f  00 00 00 00

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
- F is probably flags
- R is the rain bucket tip count
- X is CRC-8, poly 0x31, init 0x00
- A is SUM-8

Some payloads:

    4000 cd6f 1000 00  64 f0 ; 00 027b 0000
    4000 cd6f 1000 01  55 e2 ; 00 02f6 0000
    4000 cd6f 1000 02  06 94 ; 00 02ed 0000
    4000 cd6f 1000 03  37 c6 ; 00 02db 0000
    4000 cd6f 1000 04  a0 30 ; 00 02b7 0000
    4000 cd6f 1000 05  91 22 ; 00 02de 0000
    4000 cd6f 1000 06  c2 54 ; 00 02bd 0000
    4000 cd6f 1000 07  f3 86 ; 00 027b 0000
    4000 cd6f 1000 08  dd 71 ; 00 02f6 0000
    4000 cd6f 1000 09  ec 81 ; 00 02ed 0000
    4000 cd6f 1000 0a  bf 55 ; 00 02db 0000
*/

#include "decoder.h"

static int ambientweather_whx_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int events = 0;
    uint8_t b[14]; // actually only 6/9 bytes, no indication what the last 5 might be
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
        if (start_pos == bitbuffer->bits_per_row[row])
            continue; // no preamble detected, move to the next row
        if (decoder->verbose)
            fprintf(stderr, "%s: WH31E/WH40 detected, buffer is %d bits length\n", __func__, bitbuffer->bits_per_row[row]);

        // remove preamble, keep whole payload
        bitbuffer_extract_bytes(bitbuffer, row, start_pos + 24, b, 14 * 8);

        if (b[0] == 0x30) {
            // WH31E
            uint8_t crc = crc8(b, 6, 0x31, 0x00);
            if (crc) {
                if (decoder->verbose)
                    fprintf(stderr, "%s: WH31E bad CRC\n", __func__);
                continue;
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
                continue;
            }
            uint8_t c_sum = add_bytes(b, 8) - b[8];
            if (c_sum) {
                if (decoder->verbose)
                    fprintf(stderr, "%s: WH40 bad SUM\n", __func__);
                continue;
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
                    "channel",          "Channel",      DATA_INT,    channel,
                    "battery",          "Battery",      DATA_STRING, battery_ok ? "OK" : "LOW",
                    "rain_raw",         "Rain count",   DATA_INT,    rain_raw,
                    "data",             "Extra Data",   DATA_STRING, extra,
                    "mic",              "Integrity",    DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            events++;
        }

        else {
            if (decoder->verbose)
                fprintf(stderr, "%s: unknown message type %02x (expected 0x30/0x40)\n", __func__, b[0]);
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
        "rain_raw",
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
