/* TFA pool temperature sensor
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

	AAAAIIII IIIITTTT TTTTTTTT DDEE

- A: ?
- I: device id (changing only after reset)
- T: temperature
- D: channel number
- E: ?
*/

#include "decoder.h"

static int tfa_pool_thermometer_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	data_t *data;
	int i,device,channel;
	int temp_raw;
	float temp_f;

	for(i=1;i<8;i++){
		if(bitbuffer->bits_per_row[i]!=28){
			/*10 24 bits frame*/
			return 0;
		}
	}

	device   = (((bb[1][0]&0xF)<<4)+((bb[1][1]&0xF0)>>4));
	temp_raw = ((bb[1][1]&0xF)<<8)+bb[1][2];
	temp_f   = (temp_raw > 2048 ? temp_raw - 4096 : temp_raw) / 10.0;
	channel  = (signed short)((bb[1][3]&0xC0)>>6);

	data = data_make(
			"model",			"", 				DATA_STRING, 	_X("TFA-Pool","TFA pool temperature sensor"),
			"id",				"Id",				DATA_INT,	device,
			"channel",			"Channel",			DATA_INT,	channel,
			"temperature_C",	"Temperature",		DATA_FORMAT, 	"%.01f C",	DATA_DOUBLE,	temp_f,
			NULL);
	decoder_output_data(decoder, data);

	return 1;

}

static char *output_fields[] = {
	"model",
	"id",
	"channel",
	"temperature_C",
	NULL
};

r_device tfa_pool_thermometer = {
	.name          = "TFA pool temperature sensor",
	.modulation    = OOK_PULSE_PPM,
	.short_width   = 2000,
	.long_width    = 4600,
	.gap_limit     = 7800,
	.reset_limit   = 10000,
	.decode_fn     = &tfa_pool_thermometer_callback,
	.disabled      = 0,
	.fields        = output_fields,
};
