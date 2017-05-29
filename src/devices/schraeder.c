/* Schrader TPMS protocol
 *
 * Copyright (C) 2016 Benjamin Larsson
 * and 2017 Christian W. Zuckschwerdt <zany@triq.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/**
 * Packet payload: 1 sync nibble and 8 bytes data, 17 nibbles
 *
 * 0 12 34 56 78 9A BC DE F0
 * 7 f6 70 3a 38 b2 00 49 49
 * S PF FI II II II PP TT CC
 *
 * S = sync
 * P = preamble (0xf)
 * F = flags
 * I = id (28 bit)
 * P = pressure from 0 bar to 6.375 bar, resolution of 25mbar per bit
 * T = temperature from -50 C to 205 C (1 bit = 1 temperature count 1 C)
 * C = CRC8 from nibble 1 to E
 */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

static int schraeder_callback(bitbuffer_t *bitbuffer) {
	char time_str[LOCAL_TIME_BUFLEN];
	data_t *data;
	uint8_t b[8];
	uint32_t serial_id;
	char id_str[9];
	int flags;
	char flags_str[3];
	int pressure; // mbar
	int temperature; // deg C

	/* Reject wrong amount of bits */
	if ( bitbuffer->bits_per_row[0] != 68)
		return 0;

	/* shift the buffer 4 bits to remove the sync bits */
	bitbuffer_extract_bytes(bitbuffer, 0, 4, b, 64);

	/* Calculate the crc */
	if (b[7] != crc8(b, 7, 0x07, 0xf0)) {
		return 0;
	}

	local_time_str(0, time_str);

	/* Get serial number id */
	serial_id = (b[1]&0x0F) << 24 | b[2] << 16 | b[3] << 8 | b[4];
	sprintf(id_str, "%07X", serial_id);
	flags = (b[0]&0x0F) << 4 | b[1] >> 4;
	sprintf(flags_str, "%02x", flags);

	pressure = b[5] * 25;
	temperature = b[6] - 50;

	if (debug_output >= 1) {
		fprintf(stderr, "Schrader TPMS decoder\n");
		bitbuffer_print(bitbuffer);
		fprintf(stderr, "id = 0x%X\n", serial_id);
		fprintf(stderr, "CRC = %x\n", crc8(b, 7, 0x07, 0xf0));
	}

	data = data_make("time", "", DATA_STRING, time_str,
					"model", "", DATA_STRING, "Schrader",
					"type", "", DATA_STRING, "TPMS",
					"flags", "", DATA_STRING, flags_str,
 					"id", "ID", DATA_STRING, id_str,
					"pressure_bar",  "Pressure",    DATA_FORMAT, "%.03f bar", DATA_DOUBLE, (double)pressure/1000.0,
					"temperature_C", "Temperature", DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature,
					"mic", "Integrity", DATA_STRING, "CRC",
					NULL);

	data_acquired_handler(data);
	return 0;
}

static char *output_fields[] = {
	"time",
	"model",
	"type",
	"id",
	"flags",
	"pressure_bar",
	"temperature_C",
	"mic",
	NULL
};

r_device schraeder = {
	.name			= "Schrader TPMS",
	.modulation		= OOK_PULSE_MANCHESTER_ZEROBIT,
	.short_limit	= 120,
	.long_limit     = 0,
	.reset_limit    = 480,
	.json_callback	= &schraeder_callback,
	.disabled		= 0,
	.fields			= output_fields,
};
