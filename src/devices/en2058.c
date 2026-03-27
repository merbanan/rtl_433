/** @file
    Decoder for EN2058 (FSK_PCM, 100 µs bit width).

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

    PPPP aa aa aa aa aa ca ca III TT TT TT TT aa CC 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 NN NN TTTT

- P: 30 bit preamble (15 1-bits, 15 0-bits)
- I: 24 bit device identifier
- T: 16 bit temperature, repeated 4x
- C: 8 bit checksum
- N: message ID, increments each time
- T: trailer

The data is then repeated nine times.  There is no pause between the trailer and the next preamble.
*/

#include <arpa/inet.h>
#include "decoder.h"

static int en2058_sensor_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    double temp1 = 0.0;
    double temp2 = 0.0;
    double temp3 = 0.0;
    double temp4 = 0.0;

    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] < 174) {
        return DECODE_ABORT_LENGTH;
    }

    int offset = 0;

    // A full packet may contain up to 9 repeats (not technically rows because there's no pause between them)
    for (int i = 0; i < 9; i++) {
        uint8_t const preamble[] = {0xff, 0xfe, 0, 0};
        offset                   = bitbuffer_search(bitbuffer, 0, offset, preamble, 30);
        if (offset < 0)
            return DECODE_ABORT_EARLY;
        offset += 30; // skip this preamble on the next iteration

        // Validate the checksum
        uint8_t checksum = 0x36;
        uint8_t data_bytes[10];
        bitbuffer_extract_bytes(bitbuffer, 0, offset + 80, (uint8_t *)&data_bytes, 80);
        for (int j = 0; j < 8; j++) {
            checksum += data_bytes[j];
        }
        if (checksum != data_bytes[9]) {
            continue;
        }

        uint32_t id = 0;
        bitbuffer_extract_bytes(bitbuffer, 0, offset + 56, (uint8_t *)&id, 24);

        int16_t rawtemp;
        bitbuffer_extract_bytes(bitbuffer, 0, offset + 80, (uint8_t *)&rawtemp, 16);
        temp1 = (ntohs(rawtemp) - 900) / 10.0;
        bitbuffer_extract_bytes(bitbuffer, 0, offset + 80 + 16, (uint8_t *)&rawtemp, 16);
        temp2 = (ntohs(rawtemp) - 900) / 10.0;
        bitbuffer_extract_bytes(bitbuffer, 0, offset + 80 + 32, (uint8_t *)&rawtemp, 16);
        temp3 = (ntohs(rawtemp) - 900) / 10.0;
        bitbuffer_extract_bytes(bitbuffer, 0, offset + 80 + 48, (uint8_t *)&rawtemp, 16);
        temp4 = (ntohs(rawtemp) - 900) / 10.0;

        /* clang-format off */
        data_t *data = data_make(
                "model",            "",             DATA_STRING, "EN2058",
                "id",               "",             DATA_INT,    id,
                "mic",              "Integrity",    DATA_INT,    data_bytes[9],
                "temperature1_F",   "Temperature 1",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp1,
                "temperature2_F",   "Temperature 2",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp2,
                "temperature3_F",   "Temperature 3",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp3,
                "temperature4_F",   "Temperature 4",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp4,
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
        "mic",
        "temperature1_F",
        "temperature2_F",
        "temperature3_F",
        "temperature4_F",
        NULL,
};

r_device const en2058 = {
        .name        = "EN2058",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,  // 100 µs (your short pulse length)
        .long_width  = 100,  // 100 µs (your long pulse length - same for PCM)
        .reset_limit = 4000, // Max gap before EOM (µs) - tune based on your captures
        .decode_fn   = &en2058_sensor_decode,
        .fields      = output_fields,
        .disabled    = 0, // Set to 0 when the decoder is ready for production
};
