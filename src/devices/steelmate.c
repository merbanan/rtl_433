/* Steelmate TPMS FSK protocol
 *
 * Copyright © 2016 Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"


static int steelmate_callback(bitbuffer_t *bitbuffer) {
	char time_str[LOCAL_TIME_BUFLEN];

	local_time_str(0, time_str);
	if (debug_output >= 1) {
		fprintf(stdout, "Steelmate TPMS decoder\n");
		bitbuffer_print(bitbuffer);
	}

	return 0;
}

static char *output_fields[] = {
	"time",
	"model",
	"id",
	"flags",
	"pressure",
	"temperature_C",
	"depth",
	NULL
};

r_device steelmate = {
	.name			= "Steelmate TPMS",
	.modulation		= FSK_PULSE_MANCHESTER_ZEROBIT,
	.short_limit	= 12*4,
	.long_limit     = 0,
	.reset_limit    = 27*4,
	.json_callback	= &steelmate_callback,
	.disabled		= 0,
	.fields			= output_fields,
};
