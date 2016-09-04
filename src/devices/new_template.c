/* Template decoder
 *
 * Copyright © 2016 Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Use this as a starting point for a new decoder. */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"


static int template_callback(bitbuffer_t *bitbuffer) {
	char time_str[LOCAL_TIME_BUFLEN];
	bitrow_t *bb = bitbuffer->bb;
	uint32_t serial_id = 0;
	data_t *data;
	int i;

	/* Always reject as early as possible */
//	if ( bitbuffer->bits_per_row[0] != 68)
//		return 0;

	/* Parse bytes/nibbles */

	/* Check crc/checksum */

	/* Reject again based on crc */

	if (debug_output >= 1) {
		/* Place for random debug output */
		fprintf(stderr, "Template decoder debug section\n");
	}

	local_time_str(0, time_str);

	data = data_make("time", "", DATA_STRING, time_str,
					"model", "", DATA_STRING, "Template",
					"type", "", DATA_STRING, "Test",
					NULL);

	data_acquired_handler(data);
	return 1;
}

static char *csv_output_fields[] = {
	"time",
	"model",
	"type",
	NULL
};

/* This device is disabled by default. Enable it with -R 61 on the commandline */

r_device template = {
	.name			= "Template decoder",
	.modulation		= OOK_PULSE_PPM_RAW,
	.short_limit	= ((56+33)/2)*4,
	.long_limit     = (56+33)*4,
	.reset_limit    = (56+33)*2*4,
	.json_callback	= &template_callback,
	.disabled		= 1,
	.fields			= csv_output_fields,
};
