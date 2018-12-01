/* Ambient Weather WH31E protocol
 * 915 MHz FSK PCM Thermo-Hygrometer Sensor (bundled as Ambient Weather WS-3000-X5)
 *
 * Copyright (C) 2018 Christian W. Zuckschwerdt <zany@triq.net>
 * based on protocol analysis by James Cuff and Michele Clamp
 *
 * 56 us bit length with a warm-up of 1336 us mark(pulse), 1996 us space(gap),
 * a preamble of 48 bit flips (0xaaaaaaaaaaaa) and a 0x2dd4 sync-word.
 *
 * YY II CT TT HH XX ?? ?? ?? ?? ??
 * Y is a fixed Type Code of 0x30
 * I is a device ID
 * C is the Channel number (only the lower 3 bits)
 * T is 12bits Temperature in C, scaled by 10, offset 400
 * H is Humidity
 * X is CRC-8, poly 0x31, init 0x00
 *
 * Example packets:
 * {177} aa aa aa aa aa aa  2d d4  30 c3 8 20a 5e  df   bc 07 56 a7 ae  00 00 00 00
 * {178} aa aa aa aa aa aa  2d d4  30 44 9 21a 39  5a   b3 07 45 04 5f  00 00 00 00
 *
 * Some payloads:
 * 30 c3 81 d5 5c 2a cf 08 35 44 2c
 * 30 35 c2 2f 3c 0f a1 07 52 29 9f
 * 30 35 c2 2e 3c fb 8c 07 52 29 9f
 * 30 c9 a2 1e 40 0c 05 07 34 c6 b1
 * 30 2b b2 14 3d 94 f2 08 53 78 e6
 * 30 c9 a2 1f 40 f8 f2 07 34 c6 b1
 * 30 44 92 13 3e 0e 65 07 45 04 5f
 * 30 44 92 15 3d 07 5f 07 45 04 5f
 * 30 c3 81 d6 5b 90 35 08 35 44 2c
 */

#include "decoder.h"

static int ambientweather_wh31e_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int events = 0;
    uint8_t b[11]; // actually only 6 bytes, no indication what the other 5 might be
    int row;
    int msg_type, c_crc;
    int id, channel, battery_ok, temp_raw;
    int humidity;
    float temp_c;
    char extra[11];

    static uint8_t const preamble[] = {
            0xaa, // preamble
            0x2d, // sync word
            0xd4, // sync word
            0x30, // type code (presumed)
    };

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        // Validate message and reject it as fast as possible : check for preamble
        unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, 28);
        if (start_pos == bitbuffer->bits_per_row[row])
            continue; // no preamble detected, move to the next row
        if (decoder->verbose)
            fprintf(stderr, "Ambient Weather WH31E detected, buffer is %d bits length\n", bitbuffer->bits_per_row[row]);
        // remove preamble and keep only 64 bits
        bitbuffer_extract_bytes(bitbuffer, row, start_pos + 24, b, 11 * 8);

        if (b[0] != 0x30) {
            if (decoder->verbose)
                fprintf(stderr, "Ambient Weather WH31E unknown message type %02x (expected 0x30)\n", b[0]);
            continue;
        }

        c_crc = crc8(b, 5, 0x31, 0x00);
        if (c_crc != b[5]) {
            if (decoder->verbose)
                fprintf(stderr, "Ambient Weather WH31E bad CRC: calculated %02x, received %02x\n", c_crc, b[5]);
            continue;
        }

        msg_type   = b[0]; // fixed 0x30
        id         = b[1];
        battery_ok = b[2] >> 7;
        channel    = ((b[2] & 0x70) >> 4) + 1;
        temp_raw   = (b[2] & 0x0f) << 8 | b[3];
        temp_c     = temp_raw * 0.1 - 40.0;
        humidity   = b[4];
        sprintf(extra, "%02x%02x%02x%02x%02x", b[6], b[7], b[8], b[9], b[10]);

        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "AmbientWeather-WH31E",
                "id" ,              "",             DATA_INT, id,
                "channel",          "Channel",      DATA_INT, channel,
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
    return events;
}

/* clang-format off */
static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "battery",
    "temperature_C",
    "humidity",
    "data",
    "mic",
    NULL
};

r_device ambientweather_wh31e = {
    .name           = "Ambient Weather WH31E Thermo-Hygrometer Sensor",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 56,
    .long_width     = 56,
    .reset_limit    = 1500,
    .gap_limit      = 1800,
    .decode_fn      = &ambientweather_wh31e_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
