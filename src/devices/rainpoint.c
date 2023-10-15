/** @file
    Decoder for RainPoint soil temperature and moisture sensor.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decoder for RainPoint soil temperature and moisture sensor.

Seen on 433.9 Mhz.

Description of the Sensor:
- Humidity from 0 to 100 %
- Temperature from -10 C to 50 C

A Transmission contains three packets with Manchester coded data, note that the pause is a constant pulse, strangely.

Data layout:

    0  1  2  3  4  5  6  7  8  9  10 11    Byte index
    AA 81 F7 03 B1 04 00 12 08 00 51 15    Example data (reflected)
    ^^ ^^           could be sync word
          ^^        ID?
             ^^     unknown?
                ^^  channel (but maybe also other encoded data: 9F: CH1; B1: CH2; B7: CH3;)
                   ^^ ^^ unknown? (second byte changes between 00 and 02)
                         ^^ temperature (degrees)
                            ^^ humidity (percentage)
                               ^^ unknown?
                                  ^^    Checksum, simple 4-bit addition over 20 nibbles (reflected)
    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^       Section of data used to calculate the checksum (sum of nibbles)
                                     ^^ unknown, fixed 0x15?

Raw data:

    rtl_433 -R 0 -X 'n=RainPoint,m=OOK_PCM,s=500,l=500,r=1500'

*/

static int rainpoint_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xa9}; // with sync perhaps aaaa 6666 9556

    if (bitbuffer->num_rows != 1
            || bitbuffer->bits_per_row[0] < 232 // 24 MC bits + some preamble
            || bitbuffer->bits_per_row[0] > 3000) {
        decoder_logf(decoder, 2, __func__, "bit_per_row %u out of range", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_EARLY; // Unrecognized data
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof (preamble_pattern) * 8);

    if (start_pos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }
    start_pos += sizeof (preamble_pattern) * 8 - 2; // keep initial data bit

    bitbuffer_t msg = {0};
    unsigned len = bitbuffer_manchester_decode(bitbuffer, 0, start_pos, &msg, 12 * 8);
    if (len - start_pos != 12 * 2 * 8) {
        decoder_logf(decoder, 2, __func__, "Manchester decode failed, got %u bits", len - start_pos);
        return DECODE_ABORT_LENGTH;
    }
    bitbuffer_invert(&msg);

    uint8_t *b = msg.bb[0];
    reflect_bytes(b, 12);
    decoder_log_bitrow(decoder, 2, __func__, b, 12 * 8, "");

    // Checksum, add nibbles with carry
    int sum = add_nibbles(b, 10);
    if ((sum & 0xff) != b[10]) {
        decoder_logf(decoder, 2, __func__, "Checksum failed %04x vs %04x", b[10], sum);
        return DECODE_FAIL_MIC;
    }

    int sync     = (b[0] << 8) | b[1]; // just a guess
    int id       = (b[2] << 8) | b[3]; // just a guess
    int flags    = (b[4]);             // just a guess
    int status   = (b[5] << 8) | b[6]; // just a guess
    float temp_c = b[7];
    int moisture = b[8];
    int chan     = 0; // 9f: CH1, b1: CH2, b7: CH3
    //int batt     = 0;

    if (flags == 0x9f)
        chan = 1;
    else if (flags == 0xb1)
        chan = 2;
    else if (flags == 0xb7)
        chan = 3;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "RainPoint-Soil",
            "id",               "",             DATA_FORMAT, "%04x", DATA_INT,    id,
            "channel",          "",             DATA_INT,    chan,
            "sync",             "Sync?",        DATA_FORMAT, "%04x", DATA_INT,    sync,
            "flags",            "Flags?",       DATA_FORMAT, "%02x", DATA_INT,    flags,
            "status",           "Status?",      DATA_FORMAT, "%04x", DATA_INT,    status,
            //"battery_ok",       "Battery",      DATA_INT,    batt,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "moisture",         "Moisture",     DATA_FORMAT, "%d %%", DATA_INT, moisture,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "sync",   // TODO: remove this
        "flags",  // TODO: remove this
        "status", // TODO: remove this
        //"battery_ok",
        "temperature_C",
        "moisture",
        "mic",
        NULL,
};

r_device const rainpoint = {
        .name        = "RainPoint soil temperature and moisture sensor",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 500,
        .long_width  = 500,
        .reset_limit = 1500,
        .decode_fn   = &rainpoint_decode,
        .fields      = output_fields,
};
