/* Honda Car Key
 *
 * Identifies button event, but does not attempt to decrypt rolling code...
 *
 * Copyright (C) 2016 Adrian Stevenson
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "data.h"
#include "util.h"

static const char* command_code[] ={"boot", "unlock" , "lock",};

static const char* get_command_codes(const uint8_t* bytes) {
	return command_code[bytes[46] - 0xAA];
	}

static int hondaremote_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
        uint8_t *bytes = bitbuffer->bb[0];
	char time_str[LOCAL_TIME_BUFLEN];
	data_t *data;
	uint16_t device_id;

 for (int i = 0; i < bitbuffer->num_rows; i++)
{
	// Validate package
		if (((bitbuffer->bits_per_row[i] >385) && (bitbuffer->bits_per_row[i] <=394)) &&
	 ((bytes[0] == 0xFF ) && (bytes[38] == 0xFF))) 
	 {

	if (debug_output) {
	fprintf (stdout,"passed validation bits per row %02d\n",(bitbuffer->bits_per_row[i]));
			for (unsigned n=40; n<(50); ++n) 
				{
				fprintf(stdout,"Byte %02d", n);
				fprintf(stdout,"= %02X\n", bytes[n]);
				}
			}

//call function to lookup what button was pressed	
	const char* code = get_command_codes(bytes);
	device_id = (bytes[44]>>8|bytes[45]);
	
 /* Get time now */
	local_time_str(0, time_str);
     data = data_make(
                        "time",         "",     DATA_STRING, time_str,
                        "model",        "",     DATA_STRING, "Honda Remote",
                        "device id",    "",    DATA_INT, device_id,
                        "code",         "",    DATA_STRING, code,
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
        "device id",
        "code",
        NULL
        };


r_device hondaremote = {
	.name			= "Honda Car Key",
	.modulation		= FSK_PULSE_PWM_RAW,
	.short_limit	= 300,
	.long_limit		= 800,	// Not used
	.reset_limit	= 2000,
	.json_callback	= &hondaremote_callback,
	.disabled		= 0,
	.demod_arg		= 0,
        .fields		= output_fields	
};
