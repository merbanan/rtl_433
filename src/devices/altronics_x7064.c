/** @file
    Altronics X7064 temperature and humidity sensor.

    Copyright (C) 2022 Christian W. Zuckschwerdt <zany@triq.net>
    based on protocol decoding by Thomas White

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Altronics X7064 temperature and humidity sensor.

S.a. issue #2000

- Likely a rebranded device, sold by Altronics
- Data length is 32 bytes with a preamble of 10 bytes

Data Layout:

    // That fits nicely: aaa16e95 a3 8a ae 2d is channel 1, id 6e95, temp 38e (=910, 1 F, -17.2 C), hum 2d (=45).

    AA AC II IB AT TA AT HH AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA SS

- C: (4 bit) channel
- I: (12 bit) ID
- B: (4 bit) BP01: battery low, pairing button, 0, 1
- T: (12 bit) temperature in F, offset 900, scale 10
- H: (8 bit) humidity
- A: (4 bit) fixed values of 0xA
- S: (8 bit) checksum

Raw data:

    FF FF AA AA AA AA AA CA CA 54
    AA A1 6E 95 A6 BA A5 3B AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA D4
    AA 00 0

Format string:

    12h CH:4h ID:12h FLAGS:4b TEMP:4x4h4h4x4x4h HUM:8d 184h CHKSUM:8h 8x

Decoded example:

    aaa CH:1 ID:6e9 FLAGS:0101 TEMP:6b5 HUM:059 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa CHKSUM:d4 000

*/

static int altronics_7064_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is ffffaaaaaaaaaacaca54
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0xca, 0xca, 0x54};

    int ret = 0;
    for (unsigned row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned pos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, sizeof(preamble_pattern) * 8);

        if (pos >= bitbuffer->bits_per_row[row]) {
            decoder_log(decoder, 2, __func__, "Preamble not found");
            ret = DECODE_ABORT_EARLY;
            continue;
        }
        decoder_logf(decoder, 2, __func__, "Found row: %d", row);

        pos += sizeof(preamble_pattern) * 8;
        // we expect 32 bytes
        if (pos + 32 * 8 > bitbuffer->bits_per_row[row]) {
            decoder_log(decoder, 2, __func__, "Length check fail");
            ret = DECODE_ABORT_LENGTH;
            continue;
        }
        uint8_t b[32] = {0};
        bitbuffer_extract_bytes(bitbuffer, row, pos, b, sizeof(b) * 8);

        // verify checksum
        if ((add_bytes(b, 31) & 0xff) != b[31]) {
            decoder_log(decoder, 2, __func__, "Checksum fail");
            ret = DECODE_FAIL_MIC;
            continue;
        }

        int channel     = (b[1] & 0xf);
        int id          = (b[2] << 4) | (b[3] >> 4);
        int battery_low = (b[3] & 0x08);
        int pairing     = (b[3] & 0x04);
        int temp_raw    = ((b[4] & 0x0f) << 8) | (b[5] & 0xf0) | (b[6] & 0x0f); // weird format
        float temp_f    = (temp_raw - 900) * 0.1f;
        int humidity    = b[7];

        /* clang-format off */
        data_t *data = data_make(
                "model",            "",                 DATA_STRING, "Altronics-X7064",
                "id",               "",                 DATA_FORMAT, "%03x", DATA_INT,    id,
                "channel",          "Channel",          DATA_INT,    channel,
                "battery_ok",       "Battery_OK",       DATA_INT,    !battery_low,
                "temperature_F",    "Temperature_F",    DATA_FORMAT, "%.1f", DATA_DOUBLE, temp_f,
                "humidity",         "Humidity",         DATA_FORMAT, "%u", DATA_INT, humidity,
                "pairing",          "Pairing?",         DATA_COND,   pairing,   DATA_INT,    !!pairing,
                "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return ret;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_F",
        "humidity",
        "pairing",
        "mic",
        NULL,
};

r_device altronics_7064 = {
        .name        = "Altronics X7064 temperature and humidity sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 90,
        .long_width  = 90,
        .gap_limit   = 900,
        .reset_limit = 9000,
        .decode_fn   = &altronics_7064_decode,
        .fields      = output_fields,
};
