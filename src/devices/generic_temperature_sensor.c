/* Generic temperature sensor 1
 *
 * Copyright (C) 2015 Alexandre Coffignal
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include "data.h"
#include "rtl_433.h"
#include "util.h"


static int generic_temperature_sensor_callback(bitbuffer_t *bitbuffer) {
	data_t *data;
	char time_str[LOCAL_TIME_BUFLEN];
	uint8_t *b = bitbuffer->bb[1];
	int i,device,battery;
	float fTemp;

	for(i=1;i<10;i++){
		if(bitbuffer->bits_per_row[i]!=24){
			/*10 24 bits frame*/
			return 0;
		}
	}

	// reduce false positives
	if ((b[0] == 0 && b[1] == 0 && b[2] == 0)
			|| (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff)) {
		return 0;
	}

	//AAAAAAAA BBCCCCCC CCCCCCCC
	//AAAAAAAA     : ID
	//BBBB         : battery ?
	//CCCCCCCCCCCC : Temp

	device=(b[0]);
	battery=(b[1]&0xF0)>>4;
	fTemp=(float)((signed short)(((b[1]&0x3f)*256+b[2])<<2))/160.0;

	local_time_str(0, time_str);
	data = data_make("time", 	"", 			DATA_STRING, 					time_str,
		"model",		"", 			DATA_STRING, 	"Generic temperature sensor 1",
		"id",         	"Id",			DATA_FORMAT,	"\t %d",	DATA_INT,	device,
		"temperature_C",	"Temperature",		DATA_FORMAT, 	"%.02f C",	DATA_DOUBLE,	fTemp,
		"battery",      	"Battery?",		DATA_INT,					battery,
		NULL);
	data_acquired_handler(data);

	return 1;

}

static char *output_fields[] = {
	"time",
	"model",
	"id",
	"temperature_C",
	"battery",
	NULL
};

r_device generic_temperature_sensor = {
	.name          = "Generic temperature sensor 1",
	.modulation    = OOK_PULSE_PPM_RAW,
	.short_limit   = 3500,
	.long_limit    = 4800,
	.reset_limit   = 10000,
	.json_callback = &generic_temperature_sensor_callback,
	.disabled      = 0,
	.demod_arg     = 0,
	.fields        = output_fields,
};
