/* Honda Car Key
 *
 * Identifies event, but does not attempt to decrypt rolling code...
 *
 * Copyright (C) 2015 Tommy Vestermark
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


	// Validate package
if ((bytes[0] ==0xFF ) && (bytes[38] == 0xFF)) 
	 {
	fprintf(stdout, "Honda Car Key:\n");
	if (debug_output) {
			for (unsigned n=40; n<(50); ++n) 
				{
				fprintf(stdout,"Byte %02d", n);
				fprintf(stdout,"= %02X\n", bytes[n]);
				}
			}

//call function to lookup what button was pressed	
	const char* code = get_command_codes(bytes);

	 /* Get time now */
	local_time_str(0, time_str);
     data = data_make(
                        "time",         "",     DATA_STRING, time_str,
                        "model",        "",     DATA_STRING, "Honda Remote",
                        "code",          "",    DATA_STRING, code,
                      NULL);

       data_acquired_handler(data);
       return 1;
        }
return 1;
}

static char *output_fields[] = {
        "time",
        "model",
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
