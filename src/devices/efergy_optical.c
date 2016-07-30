/* Efergy IR is a devices that periodically reports current power consumption
 * on frequency ~433.55 MHz. The data that is transmitted consists of 8
 * bytes:
 *
 * Byte 1-4: Start bits (0000), then static data (probably device id)
 * Byte 5-7: all zeros 
 * Byte 8: Pulse Count
 * Byte 9: sample frequency (15 seconds)
 * Byte 10: seconds
 * Byte 11: bytes0-10 crc16 xmodem XOR with FF
 * Byte 12: ?crc16 xmodem
 * if pulse count <3 then power =(( pulsecount/impulse-perkwh) * (3600/seconds))
 * else  power= ((pulsecount/n_imp) * (3600/seconds))
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtl_433.h"
#include "util.h"
#include "data.h"

static int efergy_optical_callback(bitbuffer_t *bitbuffer) { 
	unsigned num_bits = bitbuffer->bits_per_row[0];
	uint8_t *bytes = bitbuffer->bb[0];
	double power, n_imp;
	double pulsecount;
	double seconds;
	data_t *data;
        char time_str[LOCAL_TIME_BUFLEN];
 	uint16_t crc;       
	uint16_t csum1;

	if (num_bits < 64 || num_bits > 100){ 
		return 0;
		}

	// The bit buffer isn't always aligned to the transmitted data, so
	// search for data start and shift out the bits which aren't part
	// of the data. The data always starts with 0000 (or 1111 if
	// gaps/pulses are mixed up).
	while ((bytes[0] & 0xf0) != 0xf0 && (bytes[0] & 0xf0) != 0x00) 
		{
		num_bits -= 1;
		if (num_bits < 64)
			{
			return 0;
			}

		for (unsigned i = 0; i < (num_bits + 7) / 8; ++i) 
			{
			bytes[i] <<= 1;
			bytes[i] |= (bytes[i + 1] & 0x80) >> 7;
			}
		}

	// Sometimes pulses and gaps are mixed up. If this happens, invert
	// all bytes to get correct interpretation.
	if (bytes[0] & 0xf0){
		for (unsigned i = 0; i < 12; ++i) 
			{
			bytes[i] = ~bytes[i];
			}
		}

	 if (debug_output){ 
     		fprintf(stdout,"Possible Efergy Optical: ");
     		bitbuffer_print(bitbuffer);
		}
	
	// Calculate checksum for bytes[0..10]
	// crc16 xmodem with start value of 0x00 and polynomic of 0x1021 is same as CRC-CCITT (0x0000)         
	// start of data, length of data=10, polynomic=0x1021, init=0x0000	  

	  csum1 = ((bytes[10]<<8)|(bytes[11]));

   	  crc = crc16_ccitt(bytes, 10, 0x1021, 0x0);

	  if (crc == csum1)
       		{ 
       		if (debug_output) {
		fprintf (stdout, "Checksum OK :) :)\n");
        	fprintf (stdout, "Calculated crc is 0x%02X\n", crc);
        	fprintf (stdout, "Received csum1 is 0x%02X\n", csum1);
		}
		// this setting depends on your electricity meter's optical output	
		n_imp = 3200;

		pulsecount =  bytes[8];
		seconds = bytes[10];

		//some logic for low pulse count not sure how I reached this formula
		if (pulsecount < 3)
			{
			power = ((pulsecount/n_imp) * (3600/seconds));
        		}
         	else
         		{
         		power = ((pulsecount/n_imp) * (3600/30));
         		}
         	/* Get time now */
	        local_time_str(0, time_str);

		 data = data_make("time",          "",            DATA_STRING, time_str,
        			"model",         "",            DATA_STRING, "Efergy Optical",
                 		"power",       "Power KWh",     DATA_FORMAT,"%.03f KWh", DATA_DOUBLE, power,
	               		   NULL);
	 	data_acquired_handler(data);
		return 0;
		}
		else 
			{
			if (debug_output)
				{ 
 				fprintf (stdout, "Checksum not OK !!!\n");
				fprintf(stdout, "Calculated crc is 0x%02X\n", crc);
				fprintf(stdout, "Received csum1 is 0x%02X\n", csum1);
				}
			}
		return 0;
		}
		
static char *output_fields[] = {
    	"time",
  	"model",
  	"power",
 	NULL
	};

r_device efergy_optical = {
	.name           = "Efergy Optical",
	.modulation     = FSK_PULSE_PWM_RAW,
	.short_limit    = 92,
	.long_limit     = 400,
	.reset_limit    = 400,
	.json_callback  = &efergy_optical_callback,
	.disabled       = 0,
	.demod_arg      = 0,
	.fields        = output_fields
};
