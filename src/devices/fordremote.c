
/* Ford Car Key
 *
 * Identifies event, but does not attempt to decrypt rolling code...
 *
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *           dd dd dd 
 * [00] {60} 74 28 c1 06 a4 1a 05 20
 * [00] {60} 74 28 c1 06 a4 1a 05 20
 * [00] {60} 74 28 c1 06 a4 1a 05 20
 * [00] {60} 74 28 c1 06 a4 1a 05 20

 */
#include "rtl_433.h"
#include "data.h"
#include "util.h"
#define FORD_BITLEN  40
#define FORD_BYTELEN 5
#define FORD_PACKETCOUNT 4


static int fordremote_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
        uint8_t *bytes = bitbuffer->bb[0];
	data_t *data;
	char time_str[LOCAL_TIME_BUFLEN];
    	local_time_str(0, time_str);
	int i;
	uint8_t id=0;
    	uint32_t device_id =0;

	unsigned bits = bitbuffer->bits_per_row[0];


 for (int i = 0; i < bitbuffer->num_rows; i++)
{
	// Validate preamble
	if ((bitbuffer->bits_per_row[i] == 6) && (bytes[00]==120))
	{
	 if (debug_output) {
                 bitbuffer_print(bitbuffer);
                }

	}

	// Validate package code goes here
	else if((bitbuffer->bits_per_row[i] > 55) && ((bb[i][0] >> 4)==7))
	{
	if (debug_output) {
		 bitbuffer_print(bitbuffer);
		}
	device_id = ((bb[i][0]<<16)| (bb[i][1]<<8)|(bb[i][2]));
	uint16_t code = (bb[i][7]);

	/* Get time now */
	local_time_str(0, time_str);

     data = data_make(
                        "time", "time", DATA_STRING, time_str,
                        "model", "model", DATA_STRING, "Ford Car Remote",
			"device_id", "device-id", DATA_INT, device_id,
                        "code", "data", DATA_INT, code,
                      NULL);

       data_acquired_handler(data);
       return 1;
        }
return 0;
}
return 0;
}

static char *output_fields[] = {
        "time",
        "model",
	"device_id",
        "code",
        NULL
        };

r_device fordremote = {
	.name		= "Ford Car Key",
	.modulation	= OOK_PULSE_PWM_TERNARY,
	.short_limit	= 312,
	.long_limit	= 625,	// Not used
	.reset_limit	= 1500,
	.json_callback	= &fordremote_callback,
	.disabled		= 0,
	.demod_arg		= 2,
        .fields		= output_fields	
};

