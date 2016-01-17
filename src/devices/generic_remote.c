/* Generic remotes and sensors using PT2260/PT2262 SC2260/SC2262 EV1527 protocol
 *
 * Tested devices:
 * SC2260
 * EV1527
 *
 * Copyright (C) 2015 Tommy Vestermark
 * Copyright (C) 2015 nebman
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "pulse_demod.h"


static int generic_remote_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	uint8_t *b = bb[0];
	
	//invert bits, short pulse is 0, long pulse is 1
	b[0] = ~b[0];
	b[1] = ~b[1];
	b[2] = ~b[2];

	unsigned bits = bitbuffer->bits_per_row[0];

	// Validate package
	if ((bits == 25)
	 && (b[3] == 0x00)	// Last bit is always 0
	 && (b[0] != 0x00) && (b[1] != 0x00) && (b[2] != 0x00)	// Reduce false positives. ID 0x00000 not supported
	) {

		uint32_t ID_16b = b[0] << 8 | b[1];
		unsigned char CMD_8b = b[2];

		fprintf(stdout, "Generic remote keypress / sensor\n");
		fprintf(stdout, "ID 16bit = 0x%04X\n", ID_16b);
		fprintf(stdout, "CMD 8bit = 0x%02X\n", CMD_8b);


		// output tristate coding

		uint32_t FULL = b[0] << 16 | b[1] << 8 | b[2];
		char c;

		fprintf(stdout, "TRISTATE = ");
		for (signed char i=22; i>=0; i-=2) {

			switch ((FULL>>i) & 0x03) {
				case 0x00:	c = '0'; break;
				case 0x01:	c = 'F'; break;
				case 0x02:	c = '!'; break; // tristate 10 is invalid code for SC226x but valid in EV1527
				case 0x03:	c = '1'; break;
				default:	c = '?'; break; // not possible anyway
			}

			fputc(c, stdout);
		}
		fprintf(stdout, "\n");

		return 1;
	}
	return 0;
}


PWM_Precise_Parameters pwm_precise_parameters_generic = {
	.pulse_tolerance	= 50,
	.pulse_sync_width	= 0,	// No sync bit used
};

r_device generic_remote = {
	.name			= "Generic Remote SC226x EV1527",
	.modulation		= OOK_PULSE_PWM_PRECISE,
	.short_limit	= 116,
	.long_limit		= 351,
	.reset_limit	= 450,
	.json_callback	= &generic_remote_callback,
	.disabled		= 0,
	.demod_arg		= (unsigned long)&pwm_precise_parameters_generic,
};
