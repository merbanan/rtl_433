/** @file
    Burnhard BBQ thermometer.

    Copyright (C) 2021 Christian Fetzer

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Burnhard BBQ thermometer.

Data format:

    1f 22 00 9052 44 1425e5 1e 8
    AA SD ?? TTTT mt XXYXYY CC ?

- AA   device code (changes when battery is removed)
- S    settings, temperature_alarm, timer_alarm, unit, timer_active
- D    thermometer probe number (0, 1, 2)
- ??   always 0 so far
- TTTT timer min and sec (bcd)
- m    meat (0=free, 1=beef, 2=veal, 3=pork, 4=chick, 5=lamb, 6=fish, 7=ham)
- t    taste (0=rare, 1=medium rare, 2=medium, 3=medium well, 4=well done, 5 when m is set to free)
- XXX  temperature setpoint in celsius (-500, /10)
- YYY  temperature (-500, /10)
- CC   CRC
- ?    a single bit (coding artefact)
*/

#include "decoder.h"

static int burnhardbbq_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b;
    data_t *data;

    bitbuffer_invert(bitbuffer);

    // All three rows contain the same information. Return on first decoded row.
    int ret = 0;
    for (int i = 0; i < bitbuffer->num_rows; ++i) {
        // A row typically has 81 bits, but the last is just a coding artefact.
        if (bitbuffer->bits_per_row[i] < 80 || bitbuffer->bits_per_row[i] > 81) {
            ret = DECODE_ABORT_LENGTH;
            continue;
        }
        b = bitbuffer->bb[i];

        // reduce false positives
        if (b[0] == 0 && b[9] == 0) {
            ret = DECODE_ABORT_EARLY;
            continue;
        }

        // Sanity check (digest last byte).
        if (lfsr_digest8_reflect(b, 9, 0x31, 0xf4) != b[9]) {
            ret = DECODE_FAIL_MIC;
            continue;
        }

        int id           = (b[0]);
        int channel      = (b[1] & 0x07);
        int temp_alarm   = (b[1] & 0x80) > 7;
        int timer_alarm  = (b[1] & 0x40) > 6;
        int timer_active = (b[1] & 0x10) > 4;
        int setpoint_raw = ((b[7] & 0x0f) << 8) | b[6];
        int temp_raw     = ((b[7] & 0xf0) << 4) | b[8];
        float setpoint_c = (setpoint_raw - 500) * 0.1f;
        float temp_c     = (temp_raw - 500) * 0.1f;

        char timer_str[6];
        snprintf(timer_str, sizeof(timer_str), "%02x:%02x", b[3], b[4] & 0x7f);

        char const *meat;
        switch (b[5] >> 4) {
        case 0: meat = "free"; break;
        case 1: meat = "beef"; break;
        case 2: meat = "veal"; break;
        case 3: meat = "pork"; break;
        case 4: meat = "chicken"; break;
        case 5: meat = "lamb"; break;
        case 6: meat = "fish"; break;
        case 7: meat = "ham"; break;
        default: meat = "";
        }

        char const *taste;
        switch (b[5] & 0x0f) {
        case 0: taste = "rare"; break;
        case 1: taste = "medium rare"; break;
        case 2: taste = "medium"; break;
        case 3: taste = "medium well"; break;
        case 4: taste = "well done"; break;
        default: taste = "";
        }

        /* clang-format off */
        data = data_make(
                "model",             "",                     DATA_STRING, "BurnhardBBQ",
                "id",                "ID",                   DATA_INT,    id,
                "channel",           "Channel",              DATA_INT,    channel,
                "temperature_C",     "Temperature",          DATA_COND,   temp_raw != 0, DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
                "setpoint_C",        "Temperature setpoint", DATA_FORMAT, "%.0f C", DATA_DOUBLE, setpoint_c,
                "temperature_alarm", "Temperature alarm",    DATA_INT,    temp_alarm,
                "timer",             "Timer",                DATA_STRING, timer_str,
                "timer_active",      "Timer active",         DATA_INT,    timer_active,
                "timer_alarm",       "Timer alarm",          DATA_INT,    timer_alarm,
                "meat",              "Meat",                 DATA_COND,   meat[0] != '\0', DATA_STRING, meat,
                "taste",             "Taste",                DATA_COND,   taste[0] != '\0', DATA_STRING, taste,
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
        "channel",
        "temperature_C",
        "setpoint_C",
        "temperature_alarm",
        "timer",
        "timer_active",
        "timer_alarm",
        "meat",
        "taste",
        NULL,
};

r_device const burnhardbbq = {
        .name        = "Burnhard BBQ thermometer",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 240,
        .long_width  = 484,
        .sync_width  = 840,
        .reset_limit = 848,
        .decode_fn   = &burnhardbbq_decode,
        .fields      = output_fields,
};
