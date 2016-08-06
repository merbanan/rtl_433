/* Doorbell implementation for Elro DB286A devices
 * 
 * Note that each device seems to have two codes, which alternate
 * for every other button press.
 * 
 * Example code: 37f62a6c80
 * 
 * Copyright (C) 2016 Fabian Zaremba <fabian@youremail.eu>
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
 
#include "rtl_433.h"
#include "pulse_demod.h"
#include "data.h"
#include "util.h"

//33 pulses per data pattern
#define	DB286A_PULSECOUNT		33
//5*8 = 40 bits, 7 trailing zero bits are encoded, too
#define DB286A_CODEBYTES		5
//Hex character count for code:
//(DB286A_CODEBYTES*8)/4 (8 bits per byte, 4 bits per hex character)
#define DB286A_CODECHARS		DB286A_CODEBYTES*2
//Minimum data pattern repetitions (14 is maximum)
#define	DB286A_MINPATTERN		5

static int doorbell_db286a_callback(bitbuffer_t *bitbuffer) {
	
	char time_str[LOCAL_TIME_BUFLEN];
	data_t *data;
	bitrow_t *bb = bitbuffer->bb;
	uint8_t *b = bb[1];
	unsigned bits = bitbuffer->bits_per_row[1];

	char id_string[DB286A_CODECHARS+1];
	char *idsp = id_string;
	
	unsigned i;
	
	if (bits != DB286A_PULSECOUNT) {
		return 0;
	}
	
	if (count_repeats(bitbuffer, 1) < DB286A_MINPATTERN) {
		return 0;
	}
	
	//Get hex string representation of code pattern
	for (i = 0; i <= DB286A_CODEBYTES; i++) {
	    idsp += sprintf(idsp, "%02x", b[i]);	
	}
	id_string[DB286A_CODECHARS] = '\0';
	
	local_time_str(0, time_str);
	
	data = data_make(
				"time",			"",		DATA_STRING, time_str,
				"model",		"",		DATA_STRING, "Elro DB286A",
				"code",			"Code",	DATA_STRING, id_string,
				NULL);
	
	data_acquired_handler(data);
	    		
	return 1;
		
}

static char *output_fields[] = {
    "time",
    "model",
    "code",
    NULL
};

r_device elro_db286a = {
	.name			= "Elro DB286A Doorbell",
	.modulation     = OOK_PULSE_PWM_RAW,
	.short_limit    = 800,
	.long_limit     = 1500*4,
	.reset_limit    = 2000*4,
	.json_callback	= &doorbell_db286a_callback,
	.disabled		= 0,
    .fields         = output_fields
};
