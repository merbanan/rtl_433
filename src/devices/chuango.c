/* Chuango Security Technology Corporation 
 *
 * PIR-900 PIR sensor
 * DWC-100 Door sensor
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"


static int chuango_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;

	// Validate package
	unsigned bits = bitbuffer->bits_per_row[0];
	if ((bits == 25)
//	 && (bb[0][1] == 0xe8) && (bb[0][2] == 0xe8)	// Check a couple of preambles
	) {
		fprintf(stdout, "Chuango Security Sensor\n");
		bitbuffer_print(bitbuffer);
		return 1;
	}
	return 0;
}


r_device chuango = {
	.name			= "Chuango Security Sensor",
	.modulation		= OOK_PULSE_PWM_RAW,
	.short_limit	= 284,	// Pulse Long 426, Short 142
	.long_limit		= 500,	// Gaps Short 142, Long  424 
	.reset_limit	= 500,	// Intermessage Gap 4298 (individually for now)
	.json_callback	= &chuango_callback,
	.disabled		= 0,
	.demod_arg		= 0,	// Do not remove startbit
};
