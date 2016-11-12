/* Danfoss CFR Thermostat sensor protocol
 *
 * Manual: http://na.heating.danfoss.com/PCMPDF/Vi.88.R1.22%20CFR%20Thrm.pdf
 *
 * Data consists of 21 nibbles of 4 bit, which are encoded with a 4B/6B encoder to an output of 126 bits (~16 encoded bytes)
 *
 * Nibble encoding:
 *  #0 -#2  -- Prefix - always <E02>
 *  #3 -#6  -- Sensor ID
 *  #7      -- Unknown
 *  #8      -- Switch setting -> 2="day", 4="timer", 8="night"
 *  #9 -#10 -- Temperature decimal <value>/256
 *  #11-#12 -- Temperature integer (in Celcius)
 *  #13-#14 -- Set point decimal <value>/256
 *  #15-#16 -- Set point integer (in Celcius)
 *  #17-#20 -- Unknown (CRC??)
 *
 * Example: <Input bits> | <output nibbles>
 *  365C A9A6 936C 4DA6 A96A 6B29 4F19 72B2 | E02 111E C4 6616 7C14 B02C
 *
 * Copyright (C) 2016 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "util.h"

// Output contains 21 nibbles
#define NUM_NIBBLES 21

// Mapping from 6 bits to 4 bits
uint8_t danfoss_decode_nibble(uint8_t byte) {
	unsigned out = 0xFF;	// Error
	switch(byte) {
		case 0x0B:	out = 0xD;	break;
		case 0x0D:	out = 0xE;	break;
		case 0x0E:	out = 0x3;	break;
		case 0x13:	out = 0x4;	break;
		case 0x15:	out = 0xA;	break;
		case 0x16:	out = 0xF;	break;
		case 0x19:	out = 0x9;	break;
		case 0x1A:	out = 0x6;	break;
		case 0x25:	out = 0x0;	break;
		case 0x26:	out = 0x7;	break;
		case 0x29:	out = 0x1;	break;
		case 0x2A:	out = 0x5;	break;
		case 0x2C:	out = 0xC;	break;
		case 0x31:	out = 0xB;	break;
		case 0x32:	out = 0x2;	break;
		case 0x34:	out = 0x8;	break;
		default:	break;	// Error
	}
	return out;
}

static int danfoss_CFR_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	data_t *data;
 	char time_str[LOCAL_TIME_BUFLEN];

	local_time_str(0, time_str);

	// Validate package
	unsigned bits = bitbuffer->bits_per_row[0];
	if (bits >= 246 && bits <= 262) {	// Package is likely 254 always
		uint8_t *inbytes = bitbuffer->bb[0]+16;	// TODO: Search for header and extract
		uint8_t nibbles[NUM_NIBBLES];

		// Decode input bytes to nibbles
		for (unsigned n=0; n<NUM_NIBBLES; ++n) {
			uint8_t nibble = danfoss_decode_nibble(bitrow_get_byte(inbytes, n*6) >> 2);
			if (nibble > 0xF) {
				if(debug_output) fprintf(stderr, "Danfoss: 6B/4B decoding error\n");
				return 0;
			}
			nibbles[n] = nibble & 0xF;
		}

		// Validate
		if(nibbles[0] != 0xE || nibbles[1] != 0 || nibbles[2] != 2) {
			if(debug_output) fprintf(stderr, "Danfoss: Prefix error\n");
			return 0;
		}

		// Decode data
		unsigned id = (nibbles[3] << 12) | (nibbles[4] << 8) | (nibbles[5] << 4) | nibbles[6];

		char *str_sw;
		switch(nibbles[8]) {
			case 2:	 str_sw = "DAY"; break;
			case 4:  str_sw = "TIMER"; break;
			case 8:  str_sw = "NIGHT"; break;
			default: str_sw = "ERROR";
		}

		float temp_meas, temp_setp;
		temp_meas  = (float)(nibbles[ 9] << 4 | nibbles[10]) / 256.0;
		temp_meas += (float)(nibbles[11] << 4 | nibbles[12]);
		temp_setp  = (float)(nibbles[13] << 4 | nibbles[14]) / 256.0;
		temp_setp += (float)(nibbles[15] << 4 | nibbles[16]);

		// Add Raw nibble data - We need to find out about the unknown bits (CRC?, Battery status?, ...)
		char str_raw[NUM_NIBBLES+4];	// Add some extra space for line end
		for (unsigned n=0; n<NUM_NIBBLES; ++n) {
			sprintf(str_raw+n, "%01X", nibbles[n]);
		}

		// Output data
		data = data_make(
		     "time",		"",		DATA_STRING,	time_str,
		     "model",		"",		DATA_STRING,	"Danfoss CFR Thermostat",
		     "id",		"ID",		DATA_INT,	id,
		     "temperature_C", 	"Temperature",	DATA_FORMAT,	"%.2f C", DATA_DOUBLE, temp_meas,
		     "setpoint_C",	"Setpoint",	DATA_FORMAT,	"%.2f C", DATA_DOUBLE, temp_setp,
		     "switch",		"Switch",	DATA_STRING,	str_sw,
		     "raw",		"Raw",		DATA_STRING,	str_raw,	// Will be removed when more data is decoded
		     NULL);
		data_acquired_handler(data);

		return 1;
	}
	return 0;
}


r_device danfoss_CFR = {
	.name           = "Danfoss CFR Thermostat",
	.modulation     = FSK_PULSE_PCM,
	.short_limit    = 100,	// NRZ decoding
	.long_limit     = 100,	// Bit width
	.reset_limit    = 500,	// Maximum run is 4 zeroes/ones
	.json_callback  = &danfoss_CFR_callback,
	.disabled       = 0,
	.demod_arg      = 0,
};
