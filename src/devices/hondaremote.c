/* Honda Car Key
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


static int hondaremote_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
        uint8_t *bytes = bitbuffer->bb[0];

       if (debug_output){
                      fprintf(stdout,"Possible Honda Remote Optical: ");
                      bitbuffer_print(bitbuffer);
                      }
	// Validate package
if (bytes[0] ==0xFF )
	 {
		fprintf(stdout, "Honda Car Key:\n");
		fprintf(stdout, "code = ");
		for (unsigned n=40; n<(40+9); ++n) {
			fprintf(stdout, "%02X", bytes[n]);
		}
	fprintf(stdout, "\n");
	if (bytes[46]==0xAA) fprintf(stdout,"Boot unlock command \n");
	if (bytes[46]==0xAB) fprintf(stdout,"Car unlock command \n");
	if (bytes[46]==0xAC) fprintf(stdout,"Car lock command \n");
		return 1;
	}
	return 0;
}


r_device hondaremote = {
	.name			= "Honda Car Key",
//	.modulation		= OOK_PULSE_PWM_TERNARY,
//	.modulation		= OOK_PULSE_MANCHESTER_ZEROBIT,
	.modulation		= FSK_PULSE_PWM_RAW,
//	.modulation	= FSK_PULSE_PCM,
//	.modulation	= OOK_PULSE_PWM_PRECISE,
	.short_limit	= 300,
	.long_limit		= 800,	// Not used
	.reset_limit	= 2000,
	.json_callback	= &hondaremote_callback,
	.disabled		= 0,
	.demod_arg		= 0,
};
