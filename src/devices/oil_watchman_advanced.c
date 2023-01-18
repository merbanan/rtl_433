/** @file
    Watchman Sonic Advanced/Plus oil tank level monitor.
    
    Copyright (C) 2023 Gareth Potter

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Watchman Sonic Advanced/Plus oil tank level monitor.

Tested devices:
- Watchman Sonic Advanced
*/
#include "decoder.h"

static int oil_watchman_advanced_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    static const uint8_t PREAMBLE_SYNC_LENGTH_BITS = 40;
    static const uint8_t MODEL_LENGTH_BITS = 24;
    static const uint8_t BODY_LENGTH_BITS = 128;
    // total length of message is 192 bits
    // preamble is 40 bits of 10101... then the 'standard' sync 0x2dd4, then a model number, 0x0e0401
    // no need to match all the preamble; 24 bits worth should do
    uint8_t const match_pattern[8] = {0xaa, 0xaa, 0xaa, 0x2d, 0xd4, 0x0e, 0x04, 0x01};
    
    unsigned bitpos      = 0;
    int events           = 0;

    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, match_pattern, PREAMBLE_SYNC_LENGTH_BITS + MODEL_LENGTH_BITS)) + BODY_LENGTH_BITS <=
            bitbuffer->bits_per_row[0]) {

        bitpos += PREAMBLE_SYNC_LENGTH_BITS;
        // get buffer including model ID, as we need this in CRC calculation
        uint8_t msg[19];
        bitbuffer_extract_bytes(bitbuffer, 0, bitpos, msg, BODY_LENGTH_BITS + MODEL_LENGTH_BITS);
        bitpos += BODY_LENGTH_BITS + MODEL_LENGTH_BITS;

        uint8_t *b = msg;
        if (crc16(b, (BODY_LENGTH_BITS + MODEL_LENGTH_BITS) / 8, 0x8005, 0) != 0) {
                return DECODE_FAIL_MIC;
        }

        // as printed on the side of the unit
        uint32_t serial_number = (b[3] << 16) | (b[4] << 8) | b[5];        
        float temperature = 0.5 * (b[7] - 0x45); // seems about right; see discussion in issue #2306
        uint8_t depth = b[10];

        /* clang-format off */
        data_t *data = data_make(
                "model",                "Model",        DATA_STRING, "Oil-SonicAdv",
                "id",                   "ID",           DATA_FORMAT, "%08d", DATA_INT, serial_number,
                "temperature_C",        "Temperature",  DATA_DOUBLE, temperature,
                "depth_cm",             "Depth",        DATA_INT,    depth,                
                "mic",                  "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        events++;
    }
    return events;
}

static char *output_fields[] = {
        "model",
        "id",
        "maybetemp",
        "temperature_C",
        "depth_cm",
        NULL,
};

r_device oil_watchman_advanced = {
        .name        = "Watchman Sonic Advanced / Plus",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 500,
        .long_width  = 500,
        .reset_limit = 9000,
        .decode_fn   = &oil_watchman_advanced_decode,
        .fields      = output_fields,
};
