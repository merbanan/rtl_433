/* X10 sensor
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

static int X10_RF_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
	// Row [0] is sync pulse
	// Validate package
	if ((bitbuffer->bits_per_row[1] == 32)		// Dont waste time on a short package
	// && (bb[1][0] == (uint8_t)(~bb[1][1]))		// Check integrity - apparently some chips may use both bytes..
	 && (bb[1][2] == ((0xff & (~bb[1][3]))))		// Check integrity
	)
	{
		fprintf(stdout, "X10 RF:\n");
		fprintf(stdout, "data    = %02X %02X %02X %02X\n", bb[1][0], bb[1][1], bb[1][2], bb[1][3]);

		return 1;
	}
	return 0;
}


r_device X10_RF = {
	.name			= "X10 RF",
	.modulation		= OOK_PULSE_PPM_RAW,
	.short_limit	= 1100,	// Short gap 500µs, long gap 1680µs
	.long_limit		= 2800,	// Gap after sync is 4.5ms (1125)
	.reset_limit	= 6000, // Gap seen between messages is ~40ms so let's get them individually
	.json_callback	= &X10_RF_callback,
	.disabled		= 1,
	.demod_arg		= 0,
};



