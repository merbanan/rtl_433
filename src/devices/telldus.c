/* 
 * Telldus Thermo- & Hygrometer (312623, F007TPH)
 * Proove Fridge/Freezer thermometer (311433)
 *
 * A complete message is 49 bits:
 *      1-bit starter bit
 *      48-bit data packet 
 *
 * 48-bit data packet format:
 *
 * 00000000 AAAAABBB CCCCDDDD DDDDDDDD EEEEEEEE FFFFFFFF 
 *
 * The first byte is always 00000000.
 * A -	product id: this one never changes. 10110 (22)
 * B -	channel: [CH1: 111] [CH2: 110] [CH3: 101] [CH4: 100] [CH5: 011] [CH6: 010] [CH7: 001] [CH8: 000]. Flipping bits + 1 gives channel.
 * C -	sensor id: for models that have two sensors (and two displays) these four bits vary from 1000 (inside sensor) to 0111 (outside sensor).
 * D -	temperature in Celsius: all of the bits have to be flipped and then divide by 10. MSB is a sign bit.
 * E - 	Humidity: flip bits and it gives humidity-%. The byte is 00000000 if there is no humidity sensor.
 * F -	CRC: non-standard CRC-8, 8 bits. 
 *		http://reveng.sourceforge.net
 *		width=8  poly=0x31  init=0x2e  refin=false  refout=false  xorout=0x00  check=0x57  residue=0x00  name=(none)
 *  
 * Copyright (C) 2017 Mika Kuivalainen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtl_433.h"
#include "util.h"

#define BITLEN		48
#define PACKETLEN	6
#define STARTBYTE	0x00
#define INIT_CRC	0x2e

static int telldus_callback(bitbuffer_t *bitbuffer) 
{
	char time_str[LOCAL_TIME_BUFLEN];
	uint8_t *bb;
	unsigned int i;
	uint8_t sensor_id;
	uint8_t r_crc, c_crc;
	uint8_t channel;
	uint8_t humidity;
	uint8_t message[PACKETLEN-1];
	int tmp;
	float temperature;
	data_t *data;

	/* Correct number of rows? */
	if (bitbuffer->num_rows != 2) {
		if (debug_output) {
			fprintf(stderr, "%s %s: wrong number of rows (%d)\n", 
				time_str, __func__, bitbuffer->num_rows);
		}
		return 0;
	}

	/* Correct bit length? */
	if (bitbuffer->bits_per_row[1] != BITLEN) {
		if (debug_output) {
			fprintf(stderr, "%s %s: wrong number of bits (%d)\n", 
				time_str, __func__, bitbuffer->bits_per_row[0]);
		}
		return 0;
	}

	bb = bitbuffer->bb[1];

	/* Correct start sequence? */
	if (bb[0] != STARTBYTE) {
		if (debug_output) {
			fprintf(stderr, "%s %s: wrong start byte\n", time_str, __func__);
		}
		return 0;
	}

	/* If debug enabled, print the raw information */
	if (debug_output) {
		fprintf(stderr, "%s %s: recieved data = ", time_str, __func__);
		for (i = 0; i < PACKETLEN; i++) {
			fprintf(stderr, "%02x ",bb[i]);
		}
		fprintf(stderr, "\n");
	}

	/* Read message for crc calculation */
	for (i = 0; i < PACKETLEN-1; i++) {
		message[i] = bb[i];
	}

	/* Correct CRC? */
	r_crc = bb[PACKETLEN - 1]; /* Last byte in the data */
	c_crc = crc8(message, PACKETLEN-1, 0x31, INIT_CRC);

	if (r_crc != c_crc) {
		if (debug_output) {
			fprintf(stderr, "%s %s: CRC failed, calculated %x, received %x\n",
				time_str, __func__, c_crc, r_crc);
		}
		return 0;
	}

	/* Message validated, now parse the data */

	/* Invert bitbuffer */
	bitbuffer_invert(bitbuffer);
	bb = bitbuffer->bb[1];

	/* Sensor id */
	sensor_id = (bb[2] >> 4) & 0x0f;
	/* Let's rename sensor 0111 to 1, and 1000 to 2 */
	if(sensor_id == 0x07)
		sensor_id = 0x01;
	else
		sensor_id = 0x02;

	/* Channel */
	channel = (bb[1] & 0x07) + 1;

	/* Temperature */
	tmp = bb[PACKETLEN-4] & 0x0f;
	tmp <<= 8;
	tmp |= bb[PACKETLEN-3];
	if(tmp & 0x800) { /* if sign bit is 1 */
		tmp &= 0x7ff;
		tmp = ~tmp + 1;
	}
	temperature = tmp / 10.0f;

	/* Humidity */
	humidity = bb[PACKETLEN-2];

	/* Get the time */
	local_time_str(0, time_str);

	if (humidity != 0xff) { /* if there is humidity sensor */
		data = data_make("time",			"",				DATA_STRING,	time_str,
						 "model",			"",				DATA_STRING,	"Telldus/Proove thermometer",
						 "channel",			"Channel",		DATA_INT,		channel,
						 "sensor", 			"Sensor id",	DATA_INT,		sensor_id,
						 "temperature_C",	"Temperature",	DATA_FORMAT,	"%.1f C", DATA_DOUBLE, temperature,
						 "humidity",		"Humidity",		DATA_FORMAT,	"%i%%", DATA_INT, humidity,
						 NULL);
	} else {
		data = data_make("time",			"",				DATA_STRING,	time_str,
						 "model",			"",				DATA_STRING,	"Telldus/Proove thermometer",
						 "channel",			"Channel",		DATA_INT,		channel,
						 "sensor", 			"Sensor id",	DATA_INT,		sensor_id,
						 "temperature_C",	"Temperature",	DATA_FORMAT,	"%.1f C", DATA_DOUBLE, temperature,
						 "humidity",		"Humidity",		DATA_STRING,	"N/A",
						 NULL);
	}
	data_acquired_handler(data);

	return 1;
}

static char *telldus_output_fields[] = {
	"time",
	"model",
	"channel",
	"sensor",
	"temperature_C",
	"humidity",
	NULL
};

r_device telldus = {
	.name			= "Telldus/Proove thermometer",
	.modulation		= OOK_PULSE_PWM_TERNARY,
	.short_limit	= 520,
	.long_limit		= 1000,
	.reset_limit	= 1100,
	.json_callback	= &telldus_callback,
	.disabled		= 0,
	.demod_arg		= 1,
	.fields			= telldus_output_fields,
};
