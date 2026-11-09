/** @file
    Paradox Magellan wireless security sensors.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Paradox Magellan wireless security sensors (door contacts, motion, smoke, remotes), 433.92 MHz.

OOK PWM, short 200 us is 1, long 400 us is 0, bit period 600 us.
Pulses over 700 us (header 800 us, start 1200 us) are taken as sync.
A motion sensor measured 184/380 us pulses, 216/408 us gaps.

    800 us pulse, 12 x (200 us pulse, 200 us gap), 200 us pulse, 400 us gap,
    start pulse 1200 us, 400 us gap, 32 data bits, stop pulse 200 us, ~40 ms gap.

Data layout, all fields LSB first:

    SS SS EE CC

- S: 16 bit serial, low byte first
- E: 8 bit event, high nibble type, low nibble state
- C: 8 bit CRC-8, poly 0x31, init 0x00, over the 3 bytes as sent (MSB first)

Event type: 0x1 door, 0x2 motion, 0x3 smoke, 0x4-0x7 remote.

Event state for sensors: 0x0 sealed, 0x1 alarm, 0x2 tamper, 0x3 alarm and tamper,
0x8 reset, 0x9 battery low, 0xa battery ok, 0xb learn.

Example: on air `37 ae 48 28` is serial 0x75ec, event 0x12 (door, tamper).
The Flipper Zero shows the serial as the two bytes in decimal, 117236.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/magellan.c`.

Flex decoder:

    -X 'n=Paradox-Magellan,m=OOK_PWM,s=200,l=400,y=1000,r=1000'
*/

static int paradox_magellan_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = DECODE_ABORT_EARLY;
    // data row follows the start (sync) pulse, the row before it ends in the preamble
    for (int row = 1; row < bitbuffer->num_rows; ++row) {
        unsigned len = bitbuffer->bits_per_row[row];
        unsigned pre = bitbuffer->bits_per_row[row - 1];
        if (!bitbuffer->syncs_before_row[row] || (len != 32 && len != 33)) {
            ret = DECODE_ABORT_LENGTH;
            continue;
        }

        // at least 10 preamble bits, all 1
        uint8_t p[2];
        if (pre < 10) {
            ret = DECODE_ABORT_EARLY;
            continue;
        }
        bitbuffer_extract_bytes(bitbuffer, row - 1, pre - 10, p, 10);
        if (p[0] != 0xff || (p[1] & 0xc0) != 0xc0) {
            ret = DECODE_ABORT_EARLY;
            continue;
        }

        uint8_t b[4];
        bitbuffer_extract_bytes(bitbuffer, row, 0, b, 32);

        if (!b[0] && !b[1] && !b[2]) {
            ret = DECODE_FAIL_SANITY;
            continue;
        }

        if (crc8(b, 3, 0x31, 0x00) != b[3]) {
            ret = DECODE_FAIL_MIC;
            continue;
        }

        int id    = (reverse8(b[1]) << 8) | reverse8(b[0]);
        int event = reverse8(b[2]);
        int type  = event >> 4;
        int state = event & 0x0f;

        char const *subtype = "";
        if (type == 1) {
            subtype = "door";
        }
        else if (type == 2) {
            subtype = "motion";
        }
        else if (type == 3) {
            subtype = "smoke";
        }
        else if (type >= 4 && type <= 7) {
            subtype = "remote";
        }

        int sensor = type >= 1 && type <= 3;

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, "Paradox-Magellan",
                "id",           "",             DATA_FORMAT, "%04x", DATA_INT, id,
                "subtype",      "Type",         DATA_COND, *subtype, DATA_STRING, subtype,
                "event",        "Event",        DATA_FORMAT, "%02x", DATA_INT, event,
                "alarm",        "Alarm",        DATA_COND, sensor && state <= 3, DATA_INT, state & 1,
                "tamper",       "Tamper",       DATA_COND, sensor && state <= 3, DATA_INT, (state >> 1) & 1,
                "battery_ok",   "Battery",      DATA_COND, state == 0x9 || state == 0xa, DATA_INT, state == 0xa,
                "mic",          "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return ret;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "subtype",
        "event",
        "alarm",
        "tamper",
        "battery_ok",
        "mic",
        NULL,
};

r_device const paradox_magellan = {
        .name        = "Paradox Magellan security sensors",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 200,
        .long_width  = 400,
        .sync_width  = 1000,
        .reset_limit = 1000, // above the 400 us bit gap, ends the package on the ~40 ms message gap
        .decode_fn   = &paradox_magellan_decode,
        .fields      = output_fields,
};
