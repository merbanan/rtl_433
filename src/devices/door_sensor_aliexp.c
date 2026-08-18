/*
 * Generic Wireless Door/Window Sensor (AliExpress)
 *
 * Copyright (C) 2026 Roman Vohradnik <rovocz@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */




// https://www.aliexpress.com/item/1005006306240583.html
// on board is visible label : 2PW206814A

// Hot Sale Wireless Magnetic Door & Window Sensor EV1527 Coding Mode RF 433MHz for Home Security Alarm System Burglar Alarm Kits
// Wireless Magnetic Door & Window Sensor with EV1527 Coding Mode RF 433MHz for Home Security Alarm System Burglar Alarm Kits
// 
// NOTE:
// 1. This sensor can't be used independently;
// 2. It can work with all the alarm hosts in our store, such as PW-150, PG-103, PG-105, PG-106, PG-107, PG-505, etc.
// 3. It can work with all the alarm hosts as long as your host Frequency is 433mhz


/**
Magnetic contact sensor, 433.92 MHz OOK PWM.
25 bits per frame, sent ~50x continuously while button held or on state change.

Pulse timings:
- Short pulse ~430 us = 0
- Long pulse  ~1150 us = 1
- Frame gap   ~13 ms

Data layout (25 bits, MSB first):
    IIIIIIIIIIIIIIIIIIII SSSSS
    I = 20-bit device ID
    S = 5-bit state code

Known state codes:
    01100 (12) = OPEN
    10010 (18) = CLOSED

Example:
    1111101101010100010001100  ID=0xFB544  state=12  OPEN
    1111101101010100010010010  ID=0xFB544  state=18  CLOSED
*/

#include "decoder.h"

static uint32_t last_raw_seen = 0;

static int magnetic_contact_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int events = 0;

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        if (bitbuffer->bits_per_row[row] != 25) {
            continue;
        }

        uint8_t *b = bitbuffer->bb[row];

        // 25 bits, MSB first, 
        // b[0] = bity 24..17, b[1] = bity 16..9,
        // b[2] = bity 8..1,   b[3] bit7 = bit 0
        uint32_t raw = ((uint32_t)b[0] << 17)
                     | ((uint32_t)b[1] <<  9)
                     | ((uint32_t)b[2] <<  1)
                     | (b[3] >> 7);

        // Disable bad values
        if (raw == 0 || raw == 0x1FFFFFF) {
            continue;
        }

        // Deduplication: only each first burst
        if (raw == last_raw_seen) {
            continue;
        }
        last_raw_seen = raw;

        // ID = high 20 bits
        uint32_t device_id = (raw >> 5) & 0xFFFFF;
        uint8_t  state_raw = raw & 0x1F;

        const char *state_str;
        switch (state_raw) {
            case 19: state_str = "OPEN";   break;  // 
            case 13: state_str = "CLOSED"; break;  // 
            default: state_str = "UNKNOWN"; break;
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",     "",          DATA_STRING, "DoorContactAliExp",
                "id",        "Device ID", DATA_FORMAT, "%05x", DATA_INT, device_id,
                "state",     "State",     DATA_STRING, state_str,
                "state_raw", "Raw",       DATA_INT,    state_raw,
                "mic",       "Integrity", DATA_STRING, "NONE",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        events++;
    }

    return events > 0 ? events : DECODE_ABORT_EARLY;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "state",
        "state_raw",
        "mic",
        NULL,
};

r_device const door_sensor_aliexp = {
        .name        = "DoorContactAliExp",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 430,
        .long_width  = 1150,
        .reset_limit = 15000,  // >13ms space between frames
        .gap_limit   = 15000,
        .tolerance   = 150,
        .decode_fn   = &magnetic_contact_decode,
        .fields      = output_fields,
};
