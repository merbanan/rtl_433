/* Generic temperature sensor 1
 *
 * Copyright (C) 2015 Alexandre Coffignal
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"

static int generic_temperature_sensor_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;

	int i,device,battery;
	char buf[255];
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
	fprintf(stdout, "\nSensor        = Temperature event\n");
	fprintf(stdout, "Device        = %d\n", device);
	fprintf(stdout, "Battery?      = %02X\n", battery);
	fprintf(stdout, "Temp          = %f\n",fTemp);
	fprintf(stdout, "Model         = Generic temperature sensor 1\n");
	fprintf(stdout, "Received Data = %02x %02x %02x\n", bb[1][0], bb[1][1], bb[1][2]);



	return 1;
}

r_device generic_temperature_sensor = {

  .name          = "Generic temperature sensor 1",
  .modulation    = OOK_PWM_D,
  .short_limit   = 875,
  .long_limit    = 1200,
  .reset_limit   = 3000,
  .json_callback = &generic_temperature_sensor_callback,
  .disabled      = 0,
  .demod_arg     = 0,
};
