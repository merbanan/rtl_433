/* Efergy e2 classic (electricity meter)
 *
 * This electricity meter periodically reports current power consumption
 * on frequency ~433.55 MHz. The data that is transmitted consists of 8
 * bytes:
 *
 * Byte 1-4: Start bits (0000), then static data (probably device id)
 * Byte 5-7: Current power consumption
 * Byte 8: Checksum
 *
 * Power calculations come from Nathaniel Elijah's program EfergyRPI_001.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtl_433.h"
#include "util.h"
#include "data.h"
#include "math.h"

static int efergy_e2_classic_callback(bitbuffer_t *bitbuffer) {
	unsigned num_bits = bitbuffer->bits_per_row[0];
	uint8_t *bytes = bitbuffer->bb[0];
	signed char fact = 0;
	data_t *data;
	char time_str[LOCAL_TIME_BUFLEN];

	if (num_bits < 64 || num_bits > 80) {
		return 0;
	}

	// The bit buffer isn't always aligned to the transmitted data, so
	// search for data start and shift out the bits which aren't part
	// of the data. The data always starts with 0000 (or 1111 if
	// gaps/pulses are mixed up).
	while ((bytes[0] & 0xf0) != 0xf0 && (bytes[0] & 0xf0) != 0x00) {
		num_bits -= 1;
		if (num_bits < 64) {
			return 0;
		}

		for (unsigned i = 0; i < (num_bits + 7) / 8; ++i) {
			bytes[i] <<= 1;
			bytes[i] |= (bytes[i + 1] & 0x80) >> 7;
		}
	}

	// Sometimes pulses and gaps are mixed up. If this happens, invert
	// all bytes to get correct interpretation.
	if (bytes[0] & 0xf0) {
		for (unsigned i = 0; i < 8; ++i) {
			bytes[i] = ~bytes[i];
		}
	}

	unsigned checksum = 0;
	for (unsigned i = 0; i < 7; ++i) {
		checksum += bytes[i];
	}
	checksum &= 0xff;
	if (checksum != bytes[7]) {
		return 0;
	}

	if (bytes[6] > 0x80)
	{
	    fact = ((0xff - bytes[6]) + 1) * -1; /* Make a negative from a signed byte */
	}
	else
	{
	    fact = bytes[6];
	}

	uint8_t learn = (bytes[3] & 0x80)>>7;
	uint8_t interval = (((bytes[3] & 0x30)>>4)+1)*6;
	uint8_t battery = (bytes[3] & 0x40)>>6;
	float current_adc = (float)((bytes[4] << 8 | bytes[5]) << fact) / 0x8000;
	uint16_t address = bytes[2] << 8 | bytes[1];
	//fprintf(stdout, "Addr: %02X%02X,Amps: %.2f A, inter: %d s, bat: %d, learn: %d\n",bytes[1],bytes[2],current_adc,interval,battery,learn);
	//fprintf(stdout, "%04X,%.2f,%d,%d,%d\n",address,current_adc,interval,battery,learn);

	local_time_str(0, time_str);
	
	// Output data
	data = data_make(
		"time",			"Time",		DATA_STRING,	time_str,
		"model",		"",			DATA_STRING,	"Efergy e2 Sensor",
		"id",			"ID",		DATA_INT,		address,
		"current",		"",			DATA_FORMAT,	"%.2f", 	DATA_DOUBLE, current_adc,
		"interval", 	"",			DATA_INT,		interval,
		"battery",      "Battery",  DATA_STRING, 	battery ? "OK" : "LOW",
		"learn",		"",			DATA_STRING, 	battery ? "NO" : "YES",
		NULL
	); 

	data_acquired_handler(data);
	
	return 1;
}

static char *output_fields[] = {
	"time",
	"model",
	"id",
	"current",
	"interval",
	"battery",
	"learn",
	NULL
};

r_device efergy_e2_classic = {
	.name           = "Efergy e2 classic",
	.modulation     = FSK_PULSE_PWM_RAW,
	.short_limit    = 92,
	.long_limit     = 400,
	.reset_limit    = 400,
	.json_callback  = &efergy_e2_classic_callback,
	.disabled       = 0,
	.demod_arg      = 0,
	.fields         = output_fields
};
