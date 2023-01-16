/** @file
    Watchman Sonic Advanced/Plus oil tank level monitor
    
    Copyright (C) 2023 Gareth Potter

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**

Tested devices:
- Watchman Sonic Advanced
*/
#include "decoder.h"


static int oil_watchman_advanced_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // total length of message is 192 bits
    // preamble is 40 bits of 10101... then the 'standard' sync 0x2dd4, then a model number, 0x0e0401
    uint8_t const preamble_pattern[8] = {0xaa, 0xaa, 0xaa, 0x2d, 0xd4, 0x0e, 0x04, 0x01};

    uint8_t *b;
    uint8_t msg[16];
    uint32_t serial_number;
    uint8_t depth             = 0;
    uint8_t maybetemp;
    double temperature;
    data_t *data;
    unsigned bitpos      = 0;
    int events           = 0;

    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 64)) + 128 <=
            bitbuffer->bits_per_row[0]) {

        decoder_log_bitbuffer(decoder, 1, __func__, bitbuffer, "Found Watchman Sonic Advanced message");

        // Skip the matched preamble bits to point to the data
        //bitpos += 8;

        bitpos += 64;
        bitbuffer_extract_bytes(bitbuffer, 0, bitpos, msg, 128);
        bitpos += 128;

        b = msg;

        // as printed on the side of the unit
        serial_number = (b[0] << 16) | (b[1] << 8) | b[2];
        b += 3;

        b += 1; // not sure what this is yet; have so far seen values of 0xc0 and 0xd8 on one sensor and 0x80 on another
        
        // don't quite understand how this is encoded yet
        //maybetemp = (b[0] << 16) | (b[1] << 8) | b[2];
        maybetemp = b[1];
        temperature = (maybetemp - 73.0) / 3.0;
        b += 3;

        depth = b[0];

        /* clang-format off */
        data = data_make(
                "model",                "", DATA_STRING, "Oil Watchman Sonic Advanced / Plus",
                "id",                   "", DATA_FORMAT, "%08d", DATA_INT, serial_number,
                "maybetemp",            "", DATA_INT,    maybetemp,
                "temperature_C",        "", DATA_DOUBLE, temperature,
                "depth_cm",             "", DATA_INT,    depth,
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
        .decode_fn   = &oil_watchman_advanced_callback,
        .fields      = output_fields,
};
