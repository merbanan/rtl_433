/* Generic temperature sensor 1
 *
 * Copyright (C) 2015 Alexandre Coffignal
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
/*
10 24 bits frames

	IIIIIIII BBTTTTTT TTTTTTTT

- I: 8 bit ID
- B: 2 bit? Battery ?
- T: 12 bit Temp
*/

#include "decoder.h"

static int generic_temperature_sensor_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
	data_t *data;
	uint8_t *b = bitbuffer->bb[1];
	int i, device, battery;
	float temp_f;

	for (i = 1; i < 10; i++) {
		if (bitbuffer->bits_per_row[i] != 24) {
			/*10 24 bits frame*/
			return 0;
		}
	}

	// reduce false positives
	if ((b[0] == 0 && b[1] == 0 && b[2] == 0)
			|| (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff)) {
		return 0;
	}

	device  = (b[0]);
	battery = (b[1] & 0xF0) >> 4;
	temp_f  = (float)((signed short)(((b[1] & 0x3f) * 256 + b[2]) << 2)) / 160.0;

	data = data_make(
			"model",		"", 			DATA_STRING, 	_X("Generic-Temperature","Generic temperature sensor 1"),
			"id",         	"Id",			DATA_INT,	device,
			"temperature_C",	"Temperature",		DATA_FORMAT, 	"%.02f C",	DATA_DOUBLE,	temp_f,
			"battery",      	"Battery?",		DATA_INT,					battery,
			NULL);
	decoder_output_data(decoder, data);

	return 1;
}

static char *output_fields[] = {
	"model",
	"id",
	"temperature_C",
	"battery",
	NULL
};

r_device generic_temperature_sensor = {
	.name          = "Generic temperature sensor 1",
	.modulation    = OOK_PULSE_PPM,
	.short_width   = 2000,
	.long_width    = 4000,
	.gap_limit     = 4800,
	.reset_limit   = 10000,
	.decode_fn     = &generic_temperature_sensor_callback,
	.disabled      = 0,
	.fields        = output_fields,
};
