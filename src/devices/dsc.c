/* DSC sensor
 *
 *
 * Stub for decoding test data only
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"

static int DSC_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS], int16_t bits_per_row[BITBUF_ROWS]) {
   
	// Validate package
	if (bits_per_row[0] >= 46)						// Dont waste time on a short package
	{
		fprintf(stdout, "DSC:\n");

		if (debug_output)
			debug_callback(bb, bits_per_row);

		return 1;
    }
	return 0;
}


r_device DSC = {
	.id				= 12,
	.name			= "DSC (Digital Security Controls)",
	.modulation		= OOK_PULSE_PCM_RZ,
	.short_limit	= 62,	// Pulse length
	.long_limit		= 125,	// Bit length
	.reset_limit	= 1250, // Max gap
	.json_callback	= &DSC_callback,
	.disabled		= 0,
	.demod_arg		= 0,
};



