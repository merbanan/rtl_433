/** @file
    Watchman Sonic Advanced/Plus oil tank level monitor.

    Copyright (C) 2023 Gareth Potter

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Watchman Sonic Advanced/Plus oil tank level monitor.

Tested devices:
- Watchman Sonic Advanced, model code 0x0401 (seen on two devices)
- Tekelek, model code 0x0106 (seen on two devices)

The devices uses GFSK with 500 us long and short pulses.
Using -Y minmax should be sufficient to get it to work.

Total length of message including preamble is 192 bits.
The format might be most easily summarised in a BitBench string:

    PRE: 40b SYNC: 16h LEN:8d MODEL:16h ID:24d 8h TEMP:8h ?:16h DEPTH:8d VER:32h CRC:16h

Data Layout:

- 40 bits of preamble, i.e. 10101010 etc.
- 2 byte of 0x2dd4 - 'standard' sync word
- 1 byte - message length, fixed 0x0e (14)
- 2 byte - fixed 0x0401 or 0x0106 - presumably a model identifier, common at least to the devices we have tested
- 3 byte integer serial number - as printed on a label attached to the device itself
- 1 byte status:
  - 0xC0 - during the first 20ish minutes after sync with the receiver when the device is transmitting once per second
  - 0x80 - the first one or two transmissions after the sync period when the device seems to be calibrating itself
  - 0x98 - normal, live value that you'll see on every transmission when the device is up and running
- 1 byte temperature, in intervals of 0.5 degrees, offset by 0x48
- 2 byte - varying bytes which could be the raw sensor reading
- 1 byte integer depth (i.e. the distance between the sensor and the oil in the tank)
- 4 byte of 0x01050300 - constant values which could be a version number? (1.5.3.0)
- 2 byte CRC-16 poly 0x8005 init 0
*/

static int oil_watchman_advanced_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    static uint8_t const PREAMBLE_SYNC_LENGTH_BITS = 40;
    static uint8_t const HEADER_LENGTH_BITS        = 8;
    static uint8_t const BODY_LENGTH_BITS          = 144;
    // no need to match all the preamble; 24 bits worth should do
    // include part of preamble, sync-word, length, message identifier
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0xaa, 0x2d, 0xd4, 0x0e};

    unsigned bitpos = 0;
    int events      = 0;

    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, PREAMBLE_SYNC_LENGTH_BITS + HEADER_LENGTH_BITS)) + BODY_LENGTH_BITS <=
            bitbuffer->bits_per_row[0]) {

        bitpos += PREAMBLE_SYNC_LENGTH_BITS;
        // get buffer including model ID, as we need this in CRC calculation
        uint8_t msg[19];
        bitbuffer_extract_bytes(bitbuffer, 0, bitpos, msg, BODY_LENGTH_BITS + HEADER_LENGTH_BITS);
        bitpos += BODY_LENGTH_BITS + HEADER_LENGTH_BITS;

        uint8_t *b = msg;
        if (crc16(b, (BODY_LENGTH_BITS + HEADER_LENGTH_BITS) / 8, 0x8005, 0) != 0) {
            decoder_log(decoder, 2, __func__, "failed CRC check");
            return DECODE_FAIL_MIC;
        }

        int mcode = (b[1] << 8) | b[2];
        if (mcode != 0x0401 && mcode != 0x0106) {
            decoder_logf(decoder, 1, __func__, "Unknown model code %04x", mcode);
            return DECODE_FAIL_SANITY;
        }

        // as printed on the side of the unit
        uint32_t serial   = (b[3] << 16) | (b[4] << 8) | b[5];
        uint8_t status    = b[6];
        float temperature = (b[7] - 0x48) / 2; // truncate to whole number
        uint8_t depth     = b[10];

        /* clang-format off */
        data_t *data = data_make(
                "model",                "Model",        DATA_STRING, "Oil-SonicAdv",
                "id",                   "ID",           DATA_FORMAT, "%08d", DATA_INT, serial,
                "temperature_C",        "Temperature",  DATA_DOUBLE, temperature,
                "depth_cm",             "Depth",        DATA_INT,    depth,
                "status",               "Status",       DATA_FORMAT, "%02x", DATA_INT, status,
                "mic",                  "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        events++;
    }
    return events;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "depth_cm",
        "mic",
        NULL,
};

r_device const oil_watchman_advanced = {
        .name        = "Watchman Sonic Advanced / Plus, Tekelek",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 500,
        .long_width  = 500,
        .reset_limit = 12500, // allow 24 sequential 0-bit's
        .decode_fn   = &oil_watchman_advanced_decode,
        .fields      = output_fields,
};
