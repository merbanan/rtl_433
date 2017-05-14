/* RF-tech decoder
 * Also marked INFRA 217S34
 * Ewig Industries Macao
 *
 * Copyright © 2016 Erik Johannessen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

static int rftech_callback(bitbuffer_t *bitbuffer) {
	char time_str[LOCAL_TIME_BUFLEN];
	bitrow_t *bb = bitbuffer->bb;
	uint16_t sensor_id = 0;
	uint8_t button;
	uint8_t battery;
	double value;
	data_t *data;
	int r;

	local_time_str(0, time_str);

	r = bitbuffer_find_repeated_row(bitbuffer, 3, 24);

	if(r >= 0 && bitbuffer->bits_per_row[r] == 24) {
	/* Example of message:
	 * 01001001 00011010 00000100
	 *
	 * First byte is unknown, but probably id.
	 * Second byte is the integer part of the temperature.
	 * Third byte bits 0-3 is the fraction/tenths of the temperature.
	 * Third byte bit 7 is 1 with fresh batteries.
	 * Third byte bit 6 is 1 on button press.
	 *
	 * More sample messages:
	 * {24} ad 18 09 : 10101101 00011000 00001001
	 * {24} 3e 17 09 : 00111110 00010111 00001001
	 * {24} 70 17 03 : 01110000 00010111 00000011
	 * {24} 09 17 01 : 00001001 00010111 00000001
	 *
	 * With fresh batteries and button pressed:
	 * {24} c5 16 c5 : 11000101 00010110 11000101
	 *
	 */
	sensor_id = bb[r][0];
	value = (bb[r][1] & 0x7f) + (bb[r][2] & 0x0f) / 10.0;
	if(bb[r][1] & 0x80) value = -value;

	battery = (bb[r][2] & 0x80) == 0x80;
	button = (bb[r][2] & 0x60) != 0;

	data = data_make("time", "", DATA_STRING, time_str,
			 "model", "", DATA_STRING, "RF-tech",
			 "id", "Id", DATA_INT, sensor_id,
			 "battery", "Battery", DATA_STRING, battery ? "OK" : "LOW",
			 "button", "Button", DATA_INT, button,
			 "temperature", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, value,
			 NULL);

	data_acquired_handler(data);

	return 1;
	}

	return 0;
}

/*
 * List of fields to output when using CSV
 *
 * Used to determine what fields will be output in what
 * order for this devince when using -F csv.
 *
 */
static char *csv_output_fields[] = {
	"time",
	"model",
	"id",
	"battery",
	"button",
	"temperature",
	NULL
};

/*
 * r_device - registers device/callback. see rtl_433_devices.h
 *
 */

r_device rftech = {
	.name		= "RF-tech",
	.modulation	= OOK_PULSE_PPM_RAW,
	.short_limit	= 3500,
	.long_limit     = 5000,
	.reset_limit    = 10000,
	.json_callback	= &rftech_callback,
	.disabled	= 1,
	.demod_arg	= 0,
	.fields		= csv_output_fields,
};
