/* OpenEnergyMonitor.org emonTx sensor protocol
 *
 * Stub driver - to be refined
 *
 * Copyright (C) 2016 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "util.h"

static int emontx_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;

	// Validate package
	unsigned bits = bitbuffer->bits_per_row[0];
	if (bits >= 290 && bits <= 300) {
		fprintf(stdout, "emonTx stub driver:\n");
		bitbuffer_print(bitbuffer);
		return 1;
	}
	return 0;
}


r_device emontx = {
	.name           = "emonTx OpenEnergyMonitor (stub driver)",
	.modulation     = FSK_PULSE_PCM,
	.short_limit    = 50,	// NRZ decoding
	.long_limit     = 50,	// Bit width
	.reset_limit    = 3000, // 600 zeros...
	.json_callback  = &emontx_callback,
	.disabled       = 0,
	.demod_arg      = 0,
};
