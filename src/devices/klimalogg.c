/** @file
    Klimalogg/30.3180.IT sensor decoder.

    Copyright (C) 2020 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Klimalogg/30.3180.IT sensor decoder.

Working decoder and information from https://github.com/baycom/tfrec

The message is 2 bytes of sync word plus 9 bytes of data.
The whole message (including sync word) is bit reflected.

Data layout:

    0x2d 0xd4 II II sT TT HH BB SS 0x56 CC

-  2d d4: Sync word
-  II(14:0): 15 bit ID of sensor (printed on the back and displayed after powerup)
-  II(15) is either 1 or 0 (fixed, depends on the sensor)
-  s(3:0): Learning sequence 0...f, after learning fixed 8
-  TTT: Temperature in BCD in .1degC steps, offset +40degC (-> -40...+60)
-  HH(6:0): rel. Humidity in % (binary coded, no BCD!)
-  BB(7): Low battery if =1
-  BB(6:4): 110 or 111 (for 3199)
-  SS(7:4): sequence number (0...f)
-  SS(3:0): 0000 (fixed)
-  56: Type?
-  CC: CRC8 from ID to 0x56 (polynomial x^8 + x^5 + x^4  + 1)

Note: The rtl_433 generic dsp code does not work well with these signals
play with the -l option (5000-15000 range) or a high sample rate.

*/

#include "decoder.h"

static int klimalogg_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xB4, 0x2B}; // 0x2d, 0xd4 bit reflected
    uint8_t b[9] = {0};

    if (bitbuffer->bits_per_row[0] < 11 * 8) {
        return DECODE_ABORT_LENGTH;
    }

    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 16) + 16;
    if (bit_offset + 9 * 8 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, 9 * 8);

    if (b[7] != 0x6a) // 0x56 bit reflected
        return DECODE_FAIL_SANITY;

    reflect_bytes(b, 9);

    int crc = crc8(b, 9, 0x31, 0);
    if (crc)
        return DECODE_FAIL_MIC;

    /* Extract parameters */
    int id            = (b[0] & 0x7f) << 8 | b[1];
    int temp_raw      = (b[2] & 0x0f) * 100 + (b[3] >> 4) * 10 + (b[3] & 0x0f);
    float temperature = (temp_raw - 400) * 0.1f;
    int humidity      = (b[4] & 0x7f);
    int battery_low   = (b[5] & 0x80) >> 7;
    int sequence_nr   = (b[6] & 0xf0) >> 4;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Klimalogg-Pro",
            "id",               "Id",               DATA_FORMAT, "%04x", DATA_INT, id,
            "battery_ok",       "Battery",          DATA_INT,    !battery_low,
            "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C",      DATA_DOUBLE, temperature,
            "humidity",         "Humidity",         DATA_INT,    humidity,
            "sequence_nr",      "Sequence Number",  DATA_INT,    sequence_nr,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "humidity",
        "sequence_nr",
        "mic",
        NULL,
};

r_device const klimalogg = {
        .name        = "Klimalogg",
        .modulation  = OOK_PULSE_NRZS,
        .short_width = 26,
        .long_width  = 0,
        .gap_limit   = 0,
        .reset_limit = 1000,
        .decode_fn   = &klimalogg_decode,
        .disabled    = 1,
        .fields      = output_fields,
};
