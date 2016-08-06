/* Steelmate TPMS FSK protocol
 *
 * Copyright © 2016 Benjamin Larsson
 * Copyright © 2016 John Jore
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Packet payload: 9 bytes.
 * Bytes 2 to 9 are inverted Manchester with swaped MSB/LSB:
 *
 *                               0  1  2  3  4  5  6  7  8
 *                    [00] {72} 00 00 7f 3c f0 d7 ad 8e fa
 * After translating            00 00 01 c3 f0 14 4a 8e a0
 *                              SS SS AA II II PP TT BB CC
 * S = sync, (0x00)
 * A = preamble, (0x01)
 * I = id, 0xc3f0
 * P = Pressure as double the PSI, 0x14 = 10 PSI
 * T = Temperature in Fahrenheit, 0x4a = 74 'F
 * B = Battery as half the millivolt, 0x8e = 2.84 V
 * C = Checksum, adding bytes 2 to 7 modulo 256 = byte 8,(0x01+0xc3+0xf0+0x14+0x4a+0x8e) modulus 256 = 0xa0
 */


#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

static int steelmate_callback(bitbuffer_t *bitbuffer) {
	//if (debug_output >= 1) {
	//	fprintf(stdout, "Steelmate TPMS decoder\n");
	//	bitbuffer_print(bitbuffer);
	//	fprintf(stdout, "\n");
	//}

	char time_str[LOCAL_TIME_BUFLEN];
	local_time_str(0, time_str);
	bitrow_t *bb = bitbuffer->bb;

	//Loop through each row of data
	for (int i = 0; i < bitbuffer->num_rows; i++)
	{
		//Payload is inverted Manchester encoded, and reversed MSB/LSB order
		uint8_t preAmble, ID1, ID2, p1, tempFahrenheit, tmpbattery_mV, payload_checksum, calculated_checksum;
		uint16_t sensorID, battery_mV;
		float pressurePSI;
		char sensorIDhex[7];
		data_t *data;

		//Length must be 72 bits to be considered a valid packet
		if (bitbuffer->bits_per_row[i] != 72)
			continue;

		//Valid preamble? (Note, the data is still wrong order at this point. Correct pre-amble: 0x00 0x00 0x01)
		if (bb[i][0] != 0x00 || bb[i][1] != 0x00 || bb[i][2] != 0x7f)
			continue;

		//Preamble
		preAmble = ~reverse8(bb[i][2]);

		//Sensor ID
		ID1 = ~reverse8(bb[i][3]);
		ID2 = ~reverse8(bb[i][4]);

		//Pressure is stored as twice the PSI
		p1 = ~reverse8(bb[i][5]);

		//Temperature is stored in Fahrenheit. Note that the datasheet claims operational to -40'C, but can only express values from -17.8'C
		tempFahrenheit = ~reverse8(bb[i][6]);

		//Battery voltage is stored as half the mV
		tmpbattery_mV = ~reverse8(bb[i][7]);

		//Checksum is a sum of all the other values
		payload_checksum = ~reverse8(bb[i][8]);
		calculated_checksum = preAmble + ID1 + ID2 + p1 + tempFahrenheit + tmpbattery_mV;
		if (payload_checksum != calculated_checksum)
			continue;

		sensorID = (ID1 << 8) + ID2;
		sprintf(sensorIDhex, "0x%04x", sensorID);
		pressurePSI = (float)p1 / 2;
		battery_mV = tmpbattery_mV * 2;

		data = data_make("time", "", DATA_STRING, time_str,
			"type", "", DATA_STRING, "TPMS",
			"make", "", DATA_STRING, "Steelmate",
			"id", "", DATA_STRING, sensorIDhex,
			"pressure_PSI", "", DATA_DOUBLE, pressurePSI,
			"temperature_F", "", DATA_DOUBLE, (float)tempFahrenheit,
			"battery_mV", "", DATA_INT, battery_mV,
			"checksum", "", DATA_STRING, "OK",
			NULL);
		data_acquired_handler(data);

		return 1;
	}

	//Was not a Steelmate TPMS after all
	return 0;
}

static char *output_fields[] = {
	"time",
	"type",
	"make",
	"id",
	"pressure_PSI",
	"temperature_F",
	"battery_mV",
	"checksum",
	NULL
};

r_device steelmate = {
	.name			= "Steelmate TPMS",
	.modulation		= FSK_PULSE_MANCHESTER_ZEROBIT,
	.short_limit	= 12*4,
	.long_limit     = 0,
	.reset_limit    = 27*4,
	.json_callback	= &steelmate_callback,
	.disabled		= 0,
	.fields			= output_fields,
};
