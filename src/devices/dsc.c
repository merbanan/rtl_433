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
	if ((bits_per_row[0] >= 48)			// Dont waste time on a short package
	 && (bb[0][0] & 0xF0)				// First 4 bits are sync bits
	 && (bb[0][1] & 0x08)				// Start bit
	 && (bb[0][2] & 0x04)				// Start bit
	 && (bb[0][3] & 0x02)				// Start bit
	 && (bb[0][4] & 0x01)				// Start bit
	) {
		uint8_t bytes[5];
		bytes[0] = ((bb[0][0] & 0x0F) << 4) | ((bb[0][1] & 0xF0) >> 4);
		bytes[1] = ((bb[0][1] & 0x07) << 5) | ((bb[0][2] & 0xF8) >> 3);
		bytes[2] = ((bb[0][2] & 0x03) << 6) | ((bb[0][3] & 0xFA) >> 2);
		bytes[3] = ((bb[0][3] & 0x01) << 7) | ((bb[0][4] & 0xFE) >> 1);
		bytes[4] = ((bb[0][5]));

		fprintf(stdout, "DSC (Digital Security Controls):\n");
		fprintf(stdout, "data    = %02X %02X %02X %02X %02X\n", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]);

		if (debug_output)
			debug_callback(bb, bits_per_row);

		return 1;
	}
	return 0;
}


r_device DSC = {
	.name			= "DSC (Digital Security Controls)",
	.modulation		= OOK_PULSE_PCM_RZ,
	.short_limit	= 62,	// Pulse length
	.long_limit		= 125,	// Bit length
	.reset_limit	= 1250, // Max gap
	.json_callback	= &DSC_callback,
	.disabled		= 0,
	.demod_arg		= 0,
};



