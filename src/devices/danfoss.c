/* Danfoss thermostat sensor protocol
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

static int danfoss_CFR_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;

	// Validate package
	unsigned bits = bitbuffer->bits_per_row[0];
	if (bits >= 246 && bits <= 262) {	// Package is likely 254 always
		fprintf(stdout, "Danfoss CFR Thermostat:\n");
		bitbuffer_print(bitbuffer);
		return 1;
	}
	return 0;
}


r_device danfoss_CFR = {
	.name           = "Danfoss CFR Thermostat",
	.modulation     = FSK_PULSE_PCM,
	.short_limit    = 100,	// NRZ decoding
	.long_limit     = 100,	// Bit width
	.reset_limit    = 1000,	// 10 zeros...
	.json_callback  = &danfoss_CFR_callback,
	.disabled       = 1,
	.demod_arg      = 0,
};
