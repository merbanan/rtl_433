/* EC3k Energy Count Control
 * 
 * "Voltcraft Energy Count 3000" sensor sold by Conrad
 * aka “Velleman NETBSEM4” 
 * aka “La Crosse Techology Remote Cost Control Monitor – RS3620”.
 * aka "ELV Cost Control"
 *
 * Stub driver
 * 
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "util.h"

static int ec3k_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;

	// Validate package
	unsigned bits = bitbuffer->bits_per_row[0];
	if (bits >= 550 && bits <= 590) {	// Package should be around 578?!
		fprintf(stdout, "Energy Count 3000:\n");
		bitbuffer_print(bitbuffer);
		return 1;
	}
	return 0;
}


r_device ec3k = {
	.name           = "Energy Count 3000 (868.3 MHz)",
	.modulation     = FSK_PULSE_PCM,
	.short_limit    = 50,	// NRZ decoding
	.long_limit     = 50, 	// Bit width
	.reset_limit    = 800,	// 16 zeros (up to 12 seen)...
	.json_callback  = &ec3k_callback,
	.disabled       = 1,
	.demod_arg      = 0,
};



