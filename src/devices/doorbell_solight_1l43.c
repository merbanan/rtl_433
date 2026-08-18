/*
 * Solight 1L43 wireless doorbell button decoder.
 *
 * Copyright (C) 2026 Roman Vohradnik <rovocz@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */



/**
Solight 1L43 wireless doorbell button, 433.92 MHz OOK PWM.

Compatible models: 1L43, 1L44, 1L45, 1L49, 1L49B, 1L52, 1L53, 1L71, 1L72

Button sends fixed 25-bit code repeated ~20 times per press.
Frame period is exactly 8 ms (25 bits * 1ms/bit + 7.75ms gap).

Pulse timings:
- Short pulse ~252 us = 1
- Long pulse  ~748 us = 0
- Frame gap   ~7748 us

Data layout (25 bits):
    All 25 bits form a fixed device ID (set at pairing time).
    No state field - any received packet means button was pressed.

Example:
    {25}f679f78  -> ID=0x1ecf3ef  event=PRESSED
*/

#include "decoder.h"
#include <time.h>

static uint32_t solight_last_raw  = 0;
static time_t   solight_last_time = 0;

static int doorbell_solight_1l43_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int events = 0;

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        if (bitbuffer->bits_per_row[row] != 25) {
            continue;
        }

        uint8_t *b = bitbuffer->bb[row];

        // 25 bits MSB first
        uint32_t raw = ((uint32_t)b[0] << 17)
                     | ((uint32_t)b[1] <<  9)
                     | ((uint32_t)b[2] <<  1)
                     | (b[3] >> 7);

        if (raw == 0 || raw == 0x1FFFFFF) {
            continue;
        }

        // Deduplication – with window 2 seconds
        time_t now = time(NULL);
        if (raw == solight_last_raw && (now - solight_last_time) < 2) {
            continue;
        }
        solight_last_raw  = raw;
        solight_last_time = now;

        uint32_t device_id = raw & 0x1FFFFFF;

        /* clang-format off */
        data_t *data = data_make(
                "model",  "",          DATA_STRING, "Solight-1L43",
                "id",     "Device",    DATA_FORMAT, "%07x", DATA_INT, device_id,
                "event",  "Event",     DATA_STRING, "PRESSED",
                "mic",    "Integrity", DATA_STRING, "NONE",
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
        "event",
        "mic",
        NULL,
};

r_device const doorbell_solight_1l43 = {
        .name        = "Solight 1L43 Doorbell",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 252,
        .long_width  = 748,
        .reset_limit = 4000,   // < 7748us space between frames
        .gap_limit   = 1000,   // > 752us biggest internal space
        .tolerance   = 100,
        .decode_fn   = &doorbell_solight_1l43_decode,
        .fields      = output_fields,
};

