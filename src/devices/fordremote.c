/* Ford Car Key
 *
 * Identifies event, but does not attempt to decrypt rolling code...
 *
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Note: this used to have a broken PWM decoding, but is now proper DMC.
 * The output changed and the fields are very likely not as intended. 
 *
 * [00] {1} 80 : 1
 * [01] {9} 00 80 : 00000000 1
 * [02] {1} 80 : 1
 * [03] {78} 03 e0 01 e4 e0 90 52 97 39 60
 */

#include "rtl_433.h"
#include "data.h"
#include "util.h"

static int fordremote_callback(bitbuffer_t *bitbuffer) {
	data_t *data;
	char time_str[LOCAL_TIME_BUFLEN];
	uint8_t *bytes;
	int found = 0, i;
	int device_id, code;

	// expect {1} {9} {1} preamble
	for (int i = 3; i < bitbuffer->num_rows; i++) {
		if (bitbuffer->bits_per_row[i] < 78) {
			continue; // not a data row
		}

		// Validate preamble
		if (bitbuffer->bits_per_row[i - 3] != 1 || bitbuffer->bits_per_row[i - 1] != 1
				|| bitbuffer->bits_per_row[i - 2] != 9 || bitbuffer->bb[i - 2][0] != 0) {
			continue; // no valid preamble
		}

		if (debug_output) {
			bitbuffer_print(bitbuffer);
		}

		bytes = bitbuffer->bb[i];
		device_id = (bytes[0]<<16) | (bytes[1]<<8) | bytes[2];
		code = bytes[7];

		/* Get time now */
		local_time_str(0, time_str);
		data = data_make(
	   			"time",		"time",		DATA_STRING, time_str,
				"model",	"model",	DATA_STRING, "Ford Car Remote",
				"id",		"device-id",	DATA_INT, device_id,
				"code", 	"data",		DATA_INT, code,
				NULL);
		data_acquired_handler(data);

		found++;
	}
	return found;
}

static char *output_fields[] = {
	"time",
	"model",
	"id",
	"code",
	NULL
};

r_device fordremote = {
	.name			= "Ford Car Key",
	.modulation		= OOK_PULSE_DMC,
	.short_limit	= 250,  // half-bit width is 250 us
	.long_limit		= 500,	// bit width is 500 us
	.reset_limit	= 4000, // sync gap is 3500 us, preamble gap is 38400 us, packet gap is 52000 us
	.tolerance		= 50,
	.json_callback	= &fordremote_callback,
	.disabled		= 0,
	.demod_arg		= 0,
	.fields			= output_fields
};
