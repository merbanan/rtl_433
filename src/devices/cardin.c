/*
 * Cardin S466-TX2 generic garage door remote control on 27.195 Mhz
 * Remember to set de freq right with -f 27195000
 * May be useful for other Cardin product too
 *
 * Copyright (C) 2015 Denis Bodor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "decoder.h"

static int cardin_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	unsigned char dip[10] = {'-','-','-','-','-','-','-','-','-', '\0'};

	/*
	 * "11R"  = on-on    Right button used
	 * "10R"  = on-off   Right button used
	 * "01R"  = off-on   Right button used
	 * "00L?" = off-off  Left button used or right button does the same as the left
	 */
	char *rbutton[4] = { "11R", "10R", "01R", "00L?" };
	data_t *data;

	// validate message as we can
	if((bb[0][2] & 48) == 0 && bitbuffer->bits_per_row[0] == 24 && (
				(bb[0][2] & 0x0f) == 3 ||
				(bb[0][2] & 0x0f) == 9 ||
				(bb[0][2] & 0x0f) == 12 ||
				(bb[0][2] & 0x0f) == 6) ) {

/*
		fprintf(stdout, "------------------------------\n");
		fprintf(stdout, "protocol       = Cardin S466\n");
		fprintf(stdout, "message        = ");
		for (i=0 ; i<3 ; i++) {
			for (k = 7; k >= 0; k--) {
				if (bb[0][i] & 1 << k)
					fprintf(stdout, "1");
				else
					fprintf(stdout, "0");
			}
			fprintf(stdout, " ");
		}
		fprintf(stdout, "\n\n");
*/

		// Dip 1
		if(bb[0][0] & 8) {
			dip[0]='o';
			if(bb[0][1] & 8)
				dip[0]='+';
		}
		// Dip 2
		if(bb[0][0] & 16) {
			dip[1]='o';
			if(bb[0][1] & 16)
				dip[1]='+';
		}
		// Dip 3
		if(bb[0][0] & 32) {
			dip[2]='o';
			if(bb[0][1] & 32)
				dip[2]='+';
		}
		// Dip 4
		if(bb[0][0] & 64) {
			dip[3]='o';
			if(bb[0][1] & 64)
				dip[3]='+';
		}
		// Dip 5
		if(bb[0][0] & 128) {
			dip[4]='o';
			if(bb[0][1] & 128)
				dip[4]='+';
		}
		// Dip 6
		if(bb[0][2] & 128) {
			dip[5]='o';
			if(bb[0][2] & 64)
				dip[5]='+';
		}
		// Dip 7
		if(bb[0][0] & 1) {
			dip[6]='o';
			if(bb[0][1] & 1)
				dip[6]='+';
		}
		// Dip 8
		if(bb[0][0] & 2) {
			dip[7]='o';
			if(bb[0][1] & 2)
				dip[7]='+';
		}
		// Dip 9
		if(bb[0][0] & 4) {
			dip[8]='o';
			if(bb[0][1] & 4)
				dip[8]='+';
		}

		data = data_make(
			"model",      "",                       DATA_STRING, _X("Cardin-S466","Cardin S466"),
			"dipswitch",  "dipswitch",              DATA_STRING, dip,
			"rbutton",    "right button switches",  DATA_STRING, rbutton[((bb[0][2] & 15) / 3)-1],
			NULL);

		decoder_output_data(decoder, data);

		return 1;
	}
	return 0;
}

static char *output_fields[] = {
	"model",
	"dipswitch",
	"rbutton",
	NULL
};

r_device cardin = {
	.name           = "Cardin S466-TX2",
	.modulation     = OOK_PULSE_PWM,
	.short_width    = 730,
	.long_width     = 1400,
	.sync_width     = 6150,
	.gap_limit      = 1600,
	.reset_limit    = 32000,
	.decode_fn      = &cardin_callback,
	.disabled       = 0,
	.fields         = output_fields,
};
