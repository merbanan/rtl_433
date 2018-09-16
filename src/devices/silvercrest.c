/* Silvercrest remote decoder.
 *
 * Copyright (C) 2018 Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

uint8_t cmd_lu_tab[16] = {2,3,0,1,4,5,7,6,0xC,0xD,0xF,0xE,8,9,0xB,0xA};

static int silvercrest_callback(bitbuffer_t *bitbuffer) {
    char time_str[LOCAL_TIME_BUFLEN];
    uint8_t *b; // bits of a row
    uint8_t cmd;
    data_t *data;

    if (bitbuffer->bits_per_row[1] !=33)
        return 0;

    /* select second row, first might be bad */
    b = bitbuffer->bb[1];
    if ((b[0] == 0x7c) && (b[1] == 0x26)) {
        cmd = b[2] & 0xF;
	// Validate button
        if ((b[3]&0xF) != cmd_lu_tab[cmd])
            return 0;

        local_time_str(0, time_str);

        data = data_make(
            "time",  "", DATA_STRING, time_str,
            "model", "", DATA_STRING, "Silvercrest Remote Control",
            "button", "", DATA_INT, cmd,
            NULL);

        data_acquired_handler(data);

        return 1;
    }
    return 0;
}

static char *output_fields[] = {
    "time",
    "model",
    "button",
    NULL
};

r_device silvercrest = {
    .name           = "Silvercrest Remote Control",
    .modulation     = OOK_PULSE_PWM_PRECISE,
    .short_limit    = 264,
    .long_limit     = 744,
    .reset_limit    = 12000,
    .gap_limit      = 5000,
    .json_callback  = &silvercrest_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields        = output_fields,
};
