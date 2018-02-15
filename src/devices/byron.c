/* Byron doorbell routines
 *
 * Tested devices:
 * Byron BY101, Byron BY34
 *
 * Copyright © 2018 Mark Zealey
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
//#include "pulse_demod.h"
#include "util.h"

static int byron_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    uint8_t *d;

    // 20 or more rows usually, each containing 21 bits. Process the second row
    // in case first was corrupted.
    if (bitbuffer->num_rows < 2 || bitbuffer->bits_per_row[1] != 21)
        return 0;

    d = bitbuffer->bb[1];

    // begins with 11xxxxxx
    if( (d[0] & 0xc0) != 0xc0 )
        return 0;

    local_time_str(0, time_str);

    data = data_make("time", "", DATA_STRING, time_str,
        "model", "", DATA_STRING, "Byron Doorbell",
        "id", "", DATA_FORMAT, "%04x", DATA_INT, (uint16_t)(d[0] << 10) | (d[1] << 2) | (d[2] >> 6),
        // Invert bits here as pressing the button logically increments the mode, but this decrements
        "flags", "", DATA_FORMAT, "%d", DATA_INT, (~d[2] >> 3) & 0x7,
        NULL);
    data_acquired_handler(data);

    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "flags",
    NULL
};

r_device byron = {
    .name            = "Byron Doorbell",
    .modulation        = OOK_PULSE_PWM_PRECISE,
    .short_limit    = 500,
    .long_limit     = 1000,
    .reset_limit    = 3100,
    .gap_limit = 1200,
    .json_callback    = &byron_callback,
    .disabled        = 0,
    .fields            = output_fields,
};
