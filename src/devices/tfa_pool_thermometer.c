/* TFA pool temperature sensor
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


static int pool_temperature_sensor_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	data_t *data;
	char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);
	int i,device,channel;
	float fTemp;


	for(i=1;i<8;i++){
		if(bitbuffer->bits_per_row[i]!=28){
			/*10 24 bits frame*/
			return 0;
		}
	}

/*
AAAABBBB BBBBCCCC CCCCCCCC DDEE

A: ?
B: device id (changing only after reset)
C: templerature
D: channel number
E: ?
*/

        device=(((bb[1][0]&0xF)<<4)+((bb[1][1]&0xF0)>>4));
        fTemp=((signed short)(((bb[1][1]&0xF)<<8)+bb[1][2])/10.0);
	channel=(signed short)((bb[1][3]&0xC0)>>6);

	data = data_make("time", 	"", 			DATA_STRING, 					time_str,
                     "model",		"", 			DATA_STRING, 	"TFA pool temperature sensor",
		     "id",         	"Id",			DATA_FORMAT,	"\t %d",	DATA_INT,	device,
		     "channel",        	"Channel number",	DATA_FORMAT,	"\t %d",	DATA_INT,	channel,
                     "temperature_C",	"Temperature",		DATA_FORMAT, 	"%.01f C",	DATA_DOUBLE,	fTemp,
                     NULL);
    data_acquired_handler(data);
	
    return 1; 
	
}

static char *output_fields[] = {
	"time",
	"model",
	"id",
	"channel",
	"temperature_C",
	NULL
};

r_device tfa_pool_thermometer = {

  .name          = "TFA pool temperature sensor",
  .modulation    = OOK_PULSE_PPM_RAW,
  .short_limit   = 3500,
  .long_limit    = 7800,
  .reset_limit   = 10000,
  .json_callback = &pool_temperature_sensor_callback,
  .disabled      = 0,
  .demod_arg     = 0,
  .fields        = output_fields,
};

