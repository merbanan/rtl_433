/** @file
    Fine Offset / Ecowitt WH55 water leak sensor.

    Copyright (C) 2023 Christian W. Zuckschwerdt <zany@triq.net>
    Protocol analysis by \@cdavis289, test data by \@AhrBee

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

#include "decoder.h"

/**
Fine Offset / Ecowitt WH55 water leak sensor.

Test decoding with: rtl_433 -f 433.92M  -X 'n=wh55,m=FSK_PCM,s=60,l=60,g=1000,r=2500'

Note there is a collision with Fine Offset WH1050 / TFA 30.3151 weather station which starts with `aa aa aa 2d d4 5`

Data format:

                   00 01 02 03 04 05 06 07 08 09 10 11
    aa aa aa 2d d4 55 30 cf 55 04 02 89 be ae a4 20 10
                   MM FI II II BB VV VV AD XX ?? ?? ??

- Preamble: aa aa aa
- Sync: 2d d4
- M: 8 bit Family code 0x55 (ECOWITT/FineOffset WH55)
- F: 4 bit Flags, Channel (1 byte): (0=CH1, 1 = CH2, 2 = CH3, 3 = CH4)
- I: 20 bit ID, shown with leading channel in Ecowitt Web App
- B: 8 bit Battery (1 byte): 0x01 = 20%, 0x02 = 40%, 0x03 = 60%, 0x04 = 80%, 0x05 = 100%
- V: 16 bit Raw sensor measurement
- A: 2 bit Sensitivity and Alarm Setting: Left bit, 1 = High Sensitivity, 0 = Low Sensitivity, Right Bit: 1 = Alarm On, 0 = Alarm Off
- D: 6 bit Unknown?
- X: 8 bit CRC poly 0x31, init 0
- ?: 24 bit Unknown?

Format string:

    TYPE:8h FLAGS?2b CH:2d ID:20h BATT:8d RAW:16h SENS:b ALARM:b ?:6b CRC:8h ?:hh hh hh

*/

static int fineoffset_wh55_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xAA, 0x2D, 0xD4, 0x55}; // part of preamble, sync word, and message type

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY; // We expect a single row
    }

    unsigned bitpos = bitbuffer_search(bitbuffer, 0, 0, preamble, 32);
    bitpos += 24; // Start at message type
    if (bitpos + 9 * 8 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY; // No full message found
    }

    uint8_t b[12];
    bitbuffer_extract_bytes(bitbuffer, 0, bitpos, b, 12 * 8);

    if (crc8(b, 9, 0x31, 0x00)) {
        return 0; // DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 1, __func__, b, 12*8, "Message data");

    // GETTING MESSAGE TYPE
    // int msg_type  = b[0];
    // int flags     = (b[1] & 0xf);
    int channel   = (b[1] >> 4) + 1;
    int device_id = (b[2] << 8) | b[3];
    float battery = b[4] * 0.2f; // 0x01 = 20%, 0x02 = 40%, 0x03 = 60%, 0x04 = 80%, 0x05 = 100%
    int raw_value = (b[5] << 8) | b[6];

    // Left bit, 1 = High Sensitivity, 0 = Low Sensitivity, Right Bit: 1 = Alarm On, 0 = Alarm Off
    // int settings = (b[7] >> 4);
    int sensitivity = (b[7] >> 7) & 1;
    int alarm = (b[7] >> 6) & 1;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Fineoffset-WH55",
            "id",               "ID",               DATA_FORMAT, "%05X",    DATA_INT,    device_id,
            "channel",          "Channel",          DATA_INT,    channel,
            "battery_ok",       "Battery",          DATA_DOUBLE, battery,
            "raw_value",        "Raw Value",        DATA_INT, raw_value,
            "sensitivity",      "Sensitivity",      DATA_INT, sensitivity,
            "alarm",            "Alarm",            DATA_INT, alarm,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "raw_value",
        "sensitivity",
        "alarm",
        "mic",
        NULL,
};

r_device const fineoffset_wh55 = {
        .name        = "Fine Offset / Ecowitt WH55 water leak sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 60,
        .long_width  = 60,
        .reset_limit = 2500,
        .decode_fn   = &fineoffset_wh55_decode,
        .fields      = output_fields,
};
