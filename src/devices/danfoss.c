/* Danfoss CFR Thermostat sensor protocol
 *
 * Manual: http://na.heating.danfoss.com/PCMPDF/Vi.88.R1.22%20CFR%20Thrm.pdf
 *
 * No protocol information found, so protocol is reverse engineered.
 * Sensor uses FSK modulation and Pulse Code Modulated (direct bit sequence) data.
 *
 * Example received raw data package:
 *   bitbuffer:: Number of rows: 1
 *   [00] {255} 2a aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa 36 5c a9 a6 93 6c 4d a6 a9 6a 6b 29 4f 19 72 b2
 *
 * The package starts with a long (~128 bit) synchronization preamble (0xaa).
 * Sensor data consists of 21 nibbles of 4 bit, which are encoded with a 4b/6b encoder, resulting
 * in an encoded sequence of 126 bits (~16 encoded bytes)
 * The package may end with a noise bit or two.
 *
 * Example: <Received bits> | <6b/4b decoded nibbles>
 *  365C A9A6 936C 4DA6 A96A 6B29 4F19 72B2 | E02 111E C4 6616 7C14 B02C
 *
 * Nibble content:
 *  #0 -#2  -- Prefix - always 0xE02 (decoded)
 *  #3 -#6  -- Sensor ID
 *  #7      -- Message Count. Rolling counter incremented at each unique message.
 *  #8      -- Switch setting -> 2="day", 4="timer", 8="night"
 *  #9 -#10 -- Temperature decimal <value>/256
 *  #11-#12 -- Temperature integer (in Celsius)
 *  #13-#14 -- Set point decimal <value>/256
 *  #15-#16 -- Set point integer (in Celsius)
 *  #17-#20 -- CRC16, poly 0x1021, includes nibble #1-#16
 *
 * Copyright (C) 2016 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "decoder.h"

#define NUM_BYTES 10	// Output contains 21 nibbles, but skip first nibble 0xE, as it is not part of CRC and to get byte alignment
static const uint8_t HEADER[] = { 0x36, 0x5c };	// Encoded prefix. Full prefix is 3 nibbles => 18 bits (but checking 16 is ok)

// Mapping from 6 bits to 4 bits
static uint8_t danfoss_decode_nibble(uint8_t byte) {
	uint8_t out = 0xFF;	// Error
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


static int danfoss_cfr_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
	uint8_t bytes[NUM_BYTES];	// Decoded bytes with two 4 bit nibbles in each
	data_t *data;


	// Validate package
	unsigned bits = bitbuffer->bits_per_row[0];
	if (bits >= 246 && bits <= 260) {	// Normal size is 255, but allow for some noise in preamble
		// Find a package
		unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 112, HEADER, sizeof(HEADER)*8);	// Normal index is 128, skip first 14 bytes to find faster
		if (bits-bit_offset < 126) {	// Package should be at least 126 bits
			if (decoder->verbose) {
				fprintf(stderr, "Danfoss: short package. Header index: %u\n", bit_offset);
				bitbuffer_print(bitbuffer);
			}
			return 0;
		}
		bit_offset += 6;	// Skip first nibble 0xE to get byte alignment and remove from CRC calculation

		// Decode input 6 bit nibbles to output 4 bit nibbles (packed in bytes)
		for (unsigned n=0; n<NUM_BYTES; ++n) {
			uint8_t nibble_h = danfoss_decode_nibble(bitrow_get_byte(bitbuffer->bb[0], n*12+bit_offset) >> 2);
			uint8_t nibble_l = danfoss_decode_nibble(bitrow_get_byte(bitbuffer->bb[0], n*12+bit_offset+6) >> 2);
			if (nibble_h > 0xF || nibble_l > 0xF) {
				if (decoder->verbose) {
					fprintf(stderr, "Danfoss: 6b/4b decoding error\n");
					bitbuffer_print(bitbuffer);
				}
				return 0;
			}
			bytes[n] = (nibble_h << 4) | nibble_l;
		}

		// Output raw decoded data for debug
		if (decoder->verbose) {
			char str_raw[NUM_BYTES*2+4];	// Add some extra space for line end
			for (unsigned n=0; n<NUM_BYTES; ++n) {
				sprintf(str_raw+n*2, "%02X", bytes[n]);
			}
			fprintf(stderr, "Danfoss: Raw 6b/4b decoded = %s\n", str_raw);
		}

		// Validate Prefix and CRC
		uint16_t crc_calc = crc16(bytes, NUM_BYTES-2, 0x1021, 0x0000);
		if (bytes[0] != 0x02		// Somewhat redundant to header search, but checks last bits
		 || crc_calc != (((uint16_t)bytes[8] << 8) | bytes[9])
		) {
			if (decoder->verbose) fprintf(stderr, "Danfoss: Prefix or CRC error.\n");
			return 0;
		}

		// Decode data
		unsigned id = (bytes[1] << 8) | bytes[2];

		char *str_sw;
		switch (bytes[3] & 0x0F) {
			case 2:	 str_sw = "DAY"; break;
			case 4:  str_sw = "TIMER"; break;
			case 8:  str_sw = "NIGHT"; break;
			default: str_sw = "ERROR";
		}

		float temp_meas  = (float)bytes[5] + (float)bytes[4] / 256.0;
		float temp_setp  = (float)bytes[7] + (float)bytes[6] / 256.0;

		// Output data
		data = data_make(
			"model",		"",		DATA_STRING,	_X("Danfoss-CFR","Danfoss CFR Thermostat"),
			"id",		"ID",		DATA_INT,	id,
			"temperature_C", 	"Temperature",	DATA_FORMAT,	"%.2f C", DATA_DOUBLE, temp_meas,
			"setpoint_C",	"Setpoint",	DATA_FORMAT,	"%.2f C", DATA_DOUBLE, temp_setp,
			"switch",		"Switch",	DATA_STRING,	str_sw,
			"mic",           "Integrity",            DATA_STRING,    "CRC",
			NULL);
		decoder_output_data(decoder, data);

		return 1;
	}
	return 0;
}


static char *output_fields[] = {
	"model",
	"id",
	"temperature_C",
	"setpoint_C",
	"switch",
	"mic",
	NULL
};

r_device danfoss_CFR = {
	.name           = "Danfoss CFR Thermostat",
	.modulation     = FSK_PULSE_PCM,
	.short_width    = 100,	// NRZ decoding
	.long_width     = 100,	// Bit width
	.reset_limit    = 500,	// Maximum run is 4 zeroes/ones
	.decode_fn      = &danfoss_cfr_callback,
	.disabled       = 0,
	.fields         = output_fields
};
