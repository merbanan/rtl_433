/* Euroster 3000TX
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
#define	E3000TX_BITCOUNT	32
// 25/8 rounded up
#define E3000TX_CODEBYTES	4
// Hex character count for code
#define E3000TX_CODECHARS	2 * E3000TX_CODEBYTES

// JSON data
static int euroster_3000tx_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    uint8_t  *row = bitbuffer->bb[0];
    unsigned bitc = bitbuffer->bits_per_row[0];
    unsigned rowc = bitbuffer->num_rows;
    char id[E3000TX_CODECHARS+1];
    char *idsp = id;

    // Verify pulse count and row count
    if (bitc != E3000TX_BITCOUNT) {
	return 0;
    }
    if (rowc > 1) {
	return 0;
    }

    // Get hex string representation of pattern
    for (int i = 0; i < E3000TX_CODEBYTES; i++) {
        sprintf(idsp + i*2, "%02X", row[i]);
    }
    id[E3000TX_CODECHARS] = '\0';

    // Get time now
    local_time_str(0, time_str);
    data = data_make(
        "time",		"",	DATA_STRING,	time_str,
        "model",	"",	DATA_STRING,	"Euroster 3000TX",
        "content"   ,	"",	DATA_STRING,	id,
        NULL);
    data_acquired_handler(data);

    return 1;
}

// CSV data
static char *output_fields[] = {
    "time",
    "model",
    "content",
    NULL
};

// Device definition
r_device euroster_3000tx = {
    .name           = "Euroster 3000TX",
    .modulation     = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_limit    = 1000,
    .long_limit     = 0,	// not used
    .reset_limit    = 4800,
    .json_callback  = &euroster_3000tx_callback,
    .disabled       = 1,
    .demod_arg      = 0,
    .fields         = output_fields
};

