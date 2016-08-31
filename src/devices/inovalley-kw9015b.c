#include "rtl_433.h"
#include "util.h"
/* Inovalley kw9015b rain and Temperature weather station
 *
 * Copyright (C) 2015 Alexandre Coffignal
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

extern uint8_t reverse8(uint8_t x);

static int kw9015b_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;


	int i,iRain,device;
	unsigned char chksum;
	float fTemp;
	char buf[255];
	for(i=0;i<5;i++){
		if(bitbuffer->bits_per_row[i]!=36){
			/*10 24 bits frame*/
		}else{
			//AAAAAAAA BBBBBBBB BBBBBBBB CCCCCCCC DDDD
			//A : ID
			//B : Temp
			//C : Rain
			//D : checksum

			device=reverse8(bb[i][0]);
			fTemp=(float)((signed short)(reverse8(bb[i][2])*256+reverse8(bb[i][1]))) /160;
			iRain = reverse8(bb[i][3]);
			chksum=((reverse8(bb[i][0])>>4)+(reverse8(bb[i][0])&0x0F)+
				(reverse8(bb[i][1])>>4)+(reverse8(bb[i][1])&0x0F)+
				(reverse8(bb[i][2])>>4)+(reverse8(bb[i][2])&0x0F)+
				(reverse8(bb[i][3])>>4)+(reverse8(bb[i][3])&0x0F));

			if( (chksum&0x0F) == ( reverse8(bb[i][4]) &0x0F)){
				fprintf(stdout, "\nSensor        = Temperature and rain event\n");
				fprintf(stdout, "Device        = %d\n", device);
				fprintf(stdout, "Temp          = %f\n",fTemp);
				fprintf(stdout, "Rain          = %d\n",iRain);
				fprintf(stdout, "checksum      = %02x==%02x\n",chksum&0xF,reverse8(bb[i][4]));
				fprintf(stdout, "Received Data = %02X %02X %02X %02X %02X\n",
				reverse8(bb[i][0]),
				reverse8(bb[i][1]),
				reverse8(bb[i][2]),
				reverse8(bb[i][3]),
				reverse8(bb[i][4]));


				return 1;
			}
		}
	}


	return 0;

}

r_device kw9015b = {
  .name          = "Inovalley kw9015b rain and Temperature weather station",
  .modulation    = OOK_PULSE_PPM_RAW,
  .short_limit   = 3500,
  .long_limit    = 4800,
  .reset_limit   = 10000,
  .json_callback = &kw9015b_callback,
  .disabled      = 1,
  .demod_arg     = 0,
};
