/* Valeo Car Key
 *
 * Identifies event, but does not attempt to decrypt rolling code...
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"


static int valeo_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;

	// Validate package
	unsigned bits = bitbuffer->bits_per_row[0];
	if ((bits == 461)
	 && (bb[0][1] == 0xe8) && (bb[0][2] == 0xe8)	// Check a couple of preambles
	) {
		fprintf(stdout, "Valeo Car Key:\n");
		fprintf(stdout, "Rolling code = ");
		for (unsigned n=49; n<(49+9); ++n) {
			fprintf(stdout, "%02X", bb[0][n]);
		}
		fprintf(stdout, "\n");
		return 1;
	}
	return 0;
}


r_device valeo = {
	.name			= "Valeo Car Key",
	.modulation		= OOK_PULSE_MANCHESTER_ZEROBIT,
	.short_limit	= 106,
	.long_limit		= 0,	// Not used
	.reset_limit	= 400,
	.json_callback	= &valeo_callback,
	.disabled		= 1,
	.demod_arg		= 0,
};
