/* ELRO DB270 Wireless Doorbell
 *
 * Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "data.h"
#include "util.h"

// Pulses per row
#define	DB270_BITCOUNT		25
// Minimum repetitions
#define	DB270_MINROWS		4
// 25/8 rounded up
#define DB270_CODEBYTES		4
// Hex character count for code
#define DB270_CODECHARS		2 * DB270_CODEBYTES

// JSON data
static int doorbell_db270_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    uint8_t  *row = bitbuffer->bb[0];
    unsigned bitc = bitbuffer->bits_per_row[0];
    unsigned rowc = bitbuffer->num_rows;
    char id[DB270_CODECHARS+1];
    char *idsp = id;

    // Verify pulse count
    if (bitc != DB270_BITCOUNT) {
	return 0;
    }
    // Verify row count
    if (rowc < DB270_MINROWS) {
	return 0;
    }

    // Get hex string representation of code pattern
    for (int i = 0; i < DB270_CODEBYTES; i++) {
        sprintf(idsp + i*2, "%02X", row[i]);
    }
    id[DB270_CODECHARS] = '\0';

    // Get time now
    local_time_str(0, time_str);
    data = data_make(
        "time",		"",	DATA_STRING,	time_str,
        "model",	"",	DATA_STRING,	"ELRO DB270",
        "id"   ,	"",	DATA_STRING,	id,
        NULL);
    data_acquired_handler(data);

    return 1;
}

// CSV data
static char *output_fields[] = {
    "time",
    "model",
    "id",
    NULL
};

// Device definition
r_device elro_db270 = {
    .name           = "Elro DB270",
    .modulation     = OOK_PULSE_PWM_RAW,
    .short_limit    = 700,
    .long_limit     = 9500,
    .reset_limit    = 11000,
    .sync_width     = 0,
    .json_callback  = &doorbell_db270_callback,
    .disabled       = 1,
    .demod_arg      = 0,
    .fields         = output_fields
};

