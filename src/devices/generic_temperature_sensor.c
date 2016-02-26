/* Generic temperature sensor 1
 *
 * Copyright (C) 2015 Alexandre Coffignal
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Switched to new data API by 'ovrheat' Nik
 */
#include "data.h"
#include "rtl_433.h"
#include "util.h"


static int generic_temperature_sensor_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	data_t *data;
	char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);
	int i,device,battery;
	//char buf[255];
	char received_data[9];
	float fTemp;


	for(i=1;i<10;i++){
		if(bitbuffer->bits_per_row[i]!=24){
			/*10 24 bits frame*/
			return 0;
		}
	}

	//AAAAAAAA BBCCCCCC CCCCCCCC
	//AAAAAAAA     : ID
	//BBBB         : battery ?
	//CCCCCCCCCCCC : Temp

	device=(bb[1][0]);
	battery=(bb[1][1]&0xF0)>>4;
	fTemp=(float)((signed short)(((bb[1][1]&0x3f)*256+bb[1][2])<<2))/160.0;
	snprintf(received_data,sizeof(received_data),"%02x %02x %02x", bb[1][0], bb[1][1], bb[1][2]);
	
	
	//fprintf(stdout, "\nSensor        = Temperature event\n");
	//fprintf(stdout, "Device        = %d\n", device);
	//fprintf(stdout, "Battery?      = %02X\n", battery);
	//fprintf(stdout, "Temp          = %f\n",fTemp);
	//fprintf(stdout, "Model         = Generic temperature sensor 1\n");
	//fprintf(stdout, "Received Data = %02x %02x %02x\n", bb[1][0], bb[1][1], bb[1][2]);
	
	
	data = data_make("time", 			"", 			DATA_STRING, 								time_str,
                     "model", 			"", 			DATA_STRING, 	"Generic temperature sensor 1",
		             "id",          	"Id",			DATA_FORMAT,	"\t %d",	DATA_INT,		device,
                     "temperature_C",	"Temperature",	DATA_FORMAT, 	"%.02f C",	DATA_DOUBLE,	fTemp,
                     "battery",      	"Battery?",		DATA_INT,    								battery,
					 "rawdata",			"Raw data",		DATA_STRING,								received_data,
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
	"rawdata",
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
