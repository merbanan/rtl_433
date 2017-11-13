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
#include "data.h"
#include "util.h"

static int generic_remote_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	uint8_t *b = bb[0];
	data_t *data;
	char time_str[LOCAL_TIME_BUFLEN];
	char tristate[23];
	char *p = tristate;

	//invert bits, short pulse is 0, long pulse is 1
	b[0] = ~b[0];
	b[1] = ~b[1];
	b[2] = ~b[2];

	unsigned bits = bitbuffer->bits_per_row[0];

	// Validate package
	if ((bits == 25)
	 && (b[3] & 0x80)	// Last bit (MSB here) is always 1
	 && (b[0] || b[1])	// Reduce false positives. ID 0x0000 not supported
	 && (b[2])	// Reduce false positives. CMD 0x00 not supported
	) {

		uint32_t ID_16b = b[0] << 8 | b[1];
		unsigned char CMD_8b = b[2];

//		fprintf(stdout, "Generic remote keypress / sensor\n");
//		fprintf(stdout, "ID 16bit = 0x%04X\n", ID_16b);
//		fprintf(stdout, "CMD 8bit = 0x%02X\n", CMD_8b);


		// output tristate coding

		uint32_t FULL = b[0] << 16 | b[1] << 8 | b[2];
		char c;

//		fprintf(stdout, "TRISTATE = ");
		for (signed char i=22; i>=0; i-=2) {

			switch ((FULL>>i) & 0x03) {
				case 0x00:	c = '0'; break;
				case 0x01:	c = 'F'; break;
				case 0x02:	c = '!'; break; // tristate 10 is invalid code for SC226x but valid in EV1527
				case 0x03:	c = '1'; break;
				default:	c = '?'; break; // not possible anyway
			}
			*p++=c;
			*p = '\0';


			//fputc(c, stdout);
		}
//		fprintf(stdout, "\n");
		local_time_str(0, time_str);

//		fprintf(stdout, "ID 16bit = 0x%04X\n", ID_16b);
//		fprintf(stdout, "CMD 8bit = 0x%02X\n", CMD_8b);

		data = data_make(
			"time",       	"",          	DATA_STRING, time_str,
			"model",      	"",           	DATA_STRING, "Generic Remote",
			"id",         	"House Code", 	DATA_INT, ID_16b,
			"cmd",       	"Command",   	DATA_INT, CMD_8b,
			"tristate",    	"Tri-State", 	DATA_STRING, tristate,
			NULL);

		data_acquired_handler(data);


		return 1;
	}
	return 0;
}


PWM_Precise_Parameters pwm_precise_parameters_generic = {
	.pulse_tolerance	= 200, // us
	.pulse_sync_width	= 0,	// No sync bit used
};

r_device generic_remote = {
	.name			= "Generic Remote SC226x EV1527",
	.modulation		= OOK_PULSE_PWM_PRECISE,
	.short_limit	= 464,
	.long_limit		= 1404,
	.reset_limit	= 1800,
	.json_callback	= &generic_remote_callback,
	.disabled		= 0,
	.demod_arg		= (uintptr_t)&pwm_precise_parameters_generic,
};
