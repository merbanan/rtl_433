/* Schraeder TPMS protocol
 *
 * Copyright © 2016 Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/**
 * Packet payload: 8,5 bytes, 17 nibbles
 *
 *           01 23 45 67 89 AB CD EF 0
 * [00] {68} 7f 67 03 a3 8b 20 04 94 9
 *           SP UU UI II II IU UU UC C
 *
 * S = sync
 * P = preamble (0xf)
 * U = unknown
 * I = id
 * C = CRC8 from nibble 1 to E
 */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"


static int schraeder_callback(bitbuffer_t *bitbuffer) {
	char time_str[LOCAL_TIME_BUFLEN];
	bitrow_t *bb = bitbuffer->bb;
	uint32_t serial_id = 0;
	data_t *data;
	char hexid[20] = {0};
	uint8_t work_buffer[9];
	int i;

	/* Reject wrong amount of bits */
	if ( bitbuffer->bits_per_row[0] != 68)
		return 0;

	/* shift the buffer 4 bits for the crc8 calculation */
	for (i=0 ; i<8 ; i++)
		work_buffer[i] = (bb[0][i]&0x0F)<<4 | (bb[0][i+1]&0xF0) >> 4;

	/* Calculate the crc */
	if (work_buffer[7] != crc8(work_buffer, 7, 0x07, 0xf0)) {
		return 0;
	}

	local_time_str(0, time_str);

	/* Get serial number id */
	serial_id = (bb[0][2]&0x0F) << 20 | bb[0][3] << 12 | bb[0][4] << 4 | (bb[0][5]&0xF0) >> 4;
	sprintf(hexid, "%X", serial_id);

	if (debug_output >= 1) {
		fprintf(stderr, "Schraeder TPMS decoder\n");
		bitbuffer_print(bitbuffer);
		fprintf(stderr, "id = 0x%X\n", serial_id);
		fprintf(stderr, "CRC = %x\n", crc8(work_buffer, 7, 0x07, 0xf0));
	}

	data = data_make("time", "", DATA_STRING, time_str,
					"model", "", DATA_STRING, "Schraeder",
					"type", "", DATA_STRING, "TPMS",
 					"id", "ID", DATA_STRING, hexid,
					"crc", "", DATA_STRING, "OK",
					NULL);

	data_acquired_handler(data);
	return 0;
}

static char *output_fields[] = {
	"time",
	"model",
	"id",
	"flags",
	"pressure",
	"temperature_C",
	"depth",
	NULL
};

r_device schraeder = {
	.name			= "Schraeder TPMS",
	.modulation		= OOK_PULSE_MANCHESTER_ZEROBIT,
	.short_limit	= 30*4,
	.long_limit     = 0,
	.reset_limit    = 120*4,
	.json_callback	= &schraeder_callback,
	.disabled		= 0,
	.fields			= output_fields,
};
