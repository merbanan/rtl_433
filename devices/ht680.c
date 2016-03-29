/* HT680 Remote control
 *
 * Copyright (C) 2016 Igor Polovnikov
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"

static int ht680_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	data_t *data;
	
	for (uint8_t row = 0;row < bitbuffer->num_rows;row++){
		uint8_t *b = bb[row];
		if(bitbuffer->bits_per_row[row] == 40 && //Length of packet is 40
			(b[0] & 0x50) == 0x50 && //Sync mask 01010000
			(b[1] & 0x0A) == 0x0A && //Address always mask 00001010
			(b[3] & 0x82) == 0x82 && //Buttons(4,3) always mask 10000010
			(b[4] & 0x0A) == 0x0A){  //Buttons(2,1) always mask 00001010
			b[0] = b[0] & 0x0F; //Clear sync
						
			// Tristate coding
			char tristate[21];
			char *p = tristate;
			for(uint8_t byte = 0; byte < 5; byte++){
				for(int8_t bit = 7; bit > 0; bit -= 2){
					switch ((b[byte] >> (bit-1)) & 0x03){
						case 0x00:	*p++ = '0'; break;
						case 0x01:	*p++ = '?'; break; //Invalid code 01
						case 0x02:	*p++ = 'Z'; break; //Floating state Z is 10
						case 0x03:	*p++ = '1'; break;
						default: *p++ = '!'; break; //Unknown error
					}
				}
			}
			*p = '\0';
			
			data = data_make("model",	"",				DATA_STRING,	"HT680 Remote control",
							 "tristate","Tristate code",DATA_STRING,	tristate,
							 "address",	"Address",	DATA_FORMAT,	"0x%06X", DATA_INT, (b[0]<<16)+(b[1]<<8)+b[2],
							 "button1",	"Button 1",		DATA_STRING,	(((b[4]>>4) & 0x03) == 3) ? "PRESSED" : "",
							 "button2",	"Button 2",		DATA_STRING,	(((b[4]>>6) & 0x03) == 3) ? "PRESSED" : "",
							 "button3",	"Button 3",		DATA_STRING,	((((b[3]&0x7D)>>2) & 0x03) == 3) ? "PRESSED" : "",
							 "button4",	"Button 4",		DATA_STRING,	((((b[3]&0x7D)>>4) & 0x03) == 3) ? "PRESSED" : "",
							 NULL);
			data_acquired_handler(data);
			
			return 1;
		}
	}
	return 0;
}

static char *output_fields[] = {
	"model",
	"tristate",
	"address",
	"data",
	"button1",
	"button2",
	"button3",
	"button4",
	NULL
};

r_device ht680 = {
  .name          = "HT680 Remote control",
  .modulation    = OOK_PULSE_PWM_RAW,
  .short_limit   = 400,
  .long_limit    = 1200,
  .reset_limit   = 13000,
  .json_callback = &ht680_callback,
  .disabled      = 0,
  .demod_arg     = 1
};
