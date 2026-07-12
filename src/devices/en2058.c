/** @file
    Decoder for EN2058 (FSK_PCM, 100 µs bit width).

    Copyright (C) 2026 Steven Walter

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Decoder for EN2058 four probe temperature sensor.

The device uses FSK-PCM modulation with a fixed 100 µs bit width
(short_width = long_width = 100 µs).

Data layout:

    PPPP aa aa aa aa aa ca ca III TT TT TT TT aa CC ffff...ffff SS SS ffff

- P: 30 bit preamble (15 1-bits, 15 0-bits)
- a/c: fixed bytes, always observed as shown
- I: 24 bit device identifier
- T: 16 bit temperature, repeated 4x, offset 900 (i.e. raw - 900), scale 10,
  degrees Fahrenheit. A disconnected probe reads a fixed sentinel value.
- a: fixed byte, always observed as 0xaa
- C: 8 bit checksum: (0x56 + id byte 0 + id byte 1 + id byte 2 +
  sum of the 8 temperature bytes) & 0xff
- f: 144 bit fixed filler, always observed as a 00-17 (hex) counting sequence
- S: 8 bit sequence counter, sent twice back to back, increments by 2 each repeat
- f: 20 bit fixed filler, always observed as 00 01 f

The data is then repeated nine times, back to back with no pause between one
repeat's filler and the next preamble.

Verified against real captures in https://github.com/merbanan/rtl_433_tests/pull/400
(temperature1_F and temperature3_F match the physical readout exactly).
*/

#include "decoder.h"

static int en2058_sensor_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    float temp1 = 0.0f;
    float temp2 = 0.0f;
    float temp3 = 0.0f;
    float temp4 = 0.0f;

    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] < 174) {
        return DECODE_ABORT_LENGTH;
    }

    unsigned offset = 0;

    // A full packet may contain up to 9 repeats (not technically rows because there's no pause between them)
    for (int i = 0; i < 9; i++) {
        uint8_t const preamble[] = {0xff, 0xfe, 0, 0};
        offset                   = bitbuffer_search(bitbuffer, 0, offset, preamble, 30);
        if (offset >= (unsigned)bitbuffer->bits_per_row[0]) {
            return DECODE_ABORT_EARLY; // no (more) preamble found
        }
        offset += 30; // skip this preamble on the next iteration

        uint8_t id_bytes[3];
        bitbuffer_extract_bytes(bitbuffer, 0, offset + 56, id_bytes, 24);
        int id = (id_bytes[0] << 16) | (id_bytes[1] << 8) | id_bytes[2];

        // Validate the checksum
        uint8_t data_bytes[10];
        bitbuffer_extract_bytes(bitbuffer, 0, offset + 80, data_bytes, 80);
        uint8_t checksum = (0x56 + add_bytes(id_bytes, 3) + add_bytes(data_bytes, 8)) & 0xff;
        if (checksum != data_bytes[9]) {
            decoder_log(decoder, 1, __func__, "checksum fail");
            continue;
        }

        uint8_t rawtemp[2];
        bitbuffer_extract_bytes(bitbuffer, 0, offset + 80, rawtemp, 16);
        temp1 = (((rawtemp[0] << 8) | rawtemp[1]) - 900) / 10.0;
        bitbuffer_extract_bytes(bitbuffer, 0, offset + 80 + 16, rawtemp, 16);
        temp2 = (((rawtemp[0] << 8) | rawtemp[1]) - 900) / 10.0;
        bitbuffer_extract_bytes(bitbuffer, 0, offset + 80 + 32, rawtemp, 16);
        temp3 = (((rawtemp[0] << 8) | rawtemp[1]) - 900) / 10.0;
        bitbuffer_extract_bytes(bitbuffer, 0, offset + 80 + 48, rawtemp, 16);
        temp4 = (((rawtemp[0] << 8) | rawtemp[1]) - 900) / 10.0;

        // Sequence counter, sent as a duplicated byte 304 bits past the ID/temperature/checksum
        // block, increments by 2 per repeat. Only present if this repeat wasn't cut off early.
        int has_sequence = offset + 320 <= (unsigned)bitbuffer->bits_per_row[0];
        int sequence = 0;
        if (has_sequence) {
            uint8_t seq_bytes[2];
            bitbuffer_extract_bytes(bitbuffer, 0, offset + 304, seq_bytes, 16);
            sequence = seq_bytes[0];
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",            "",             DATA_STRING, "EN2058",
                "id",               "",             DATA_INT,    id,
                "temperature1_F",   "Temperature 1",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp1,
                "temperature2_F",   "Temperature 2",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp2,
                "temperature3_F",   "Temperature 3",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp3,
                "temperature4_F",   "Temperature 4",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp4,
                "sequence",         "Sequence",     DATA_COND, has_sequence, DATA_INT, sequence,
                "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return DECODE_FAIL_MIC;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature1_F",
        "temperature2_F",
        "temperature3_F",
        "temperature4_F",
        "sequence",
        "mic",
        NULL,
};

r_device const en2058 = {
        .name        = "EN2058 four probe temperature sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 4000,
        .decode_fn   = &en2058_sensor_decode,
        .fields      = output_fields,
};
