/* blyss DC5-UK-WH as sold by B&Q
 * 
 * DC5-UK-WH pair with receivers, the codes used may be specific to a receiver - use with caution
 * 
 * Copyright (C) 2016 John Jore
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtl_433.h"
#include "util.h"

static int blyss_dc5_uk_wh(bitbuffer_t *bitbuffer)
{
	bitrow_t *bb = bitbuffer->bb;

	for (int i = 0; i < bitbuffer->num_rows; i++)
	{
		//This needs additional validation, but works on mine. Suspect each DC5-UK-WH uses different codes as the transmitter
		//is paired to the receivers to avoid being triggerd by the neighbours transmitter ?!?
		if (((bb[i][0] == 0xce) && (bb[i][1] == 0x8e) && (bb[i][2] == 0x2a) && (bb[i][3] == 0x6c) && (bb[i][4] == 0x80)) || 
			((bb[i][0] == 0xe7) && (bb[i][1] == 0x37) && (bb[i][2] == 0x7a) && (bb[i][3] == 0x2c) && (bb[i][4] == 0x80)))
		{
			if (debug_output) {
				fprintf(stdout, "blyss DC5-UK-WH ringing\n");
				bitbuffer_print(bitbuffer);
			}

			data_t *data;
			char time_str[LOCAL_TIME_BUFLEN];
			local_time_str(0, time_str);

			data = data_make("time", "", DATA_STRING, time_str,
				"type", "", DATA_STRING, "doorbell",
				"make", "", DATA_STRING, "blyss",
				"model", "", DATA_STRING, "dc5-uk-wh",
				"mode", "", DATA_STRING, "ringing",
				NULL);
			data_acquired_handler(data);

			return 1;
		}
	}

	//This was not a blyss device after all
	return 0;
}

static char *output_fields[] = {
	"time",
	"type",
	"make",
	"model",
	"mode",
	NULL
};

static int blyss_callback(bitbuffer_t *bitbuffer) {
	// Validate its a 'blyss' we understand
	int rows = bitbuffer->num_rows;

	for (int i = 0; i < rows; i++) {
		unsigned bits = bitbuffer->bits_per_row[i];

		//blyss DC5-UK-WH
		if (bits == 33) { //33 bits in a "proper" message. Last row is 32
			//We have found:
			int result = blyss_dc5_uk_wh(bitbuffer);

			return result; // done
		}
	}

	//If we got this far, its not a blyss device we know of
	return 0;
}


r_device blyss = {
	.name           = "blyss DC5-UK-WH (433.92 MHz)",
	.modulation     = OOK_PULSE_PWM_RAW,
	.short_limit    = 1010,
	.long_limit     = 4000,
	.reset_limit    = 10000,	
	.json_callback  = &blyss_callback,
	.disabled       = 0,
	.demod_arg      = 0,
};
