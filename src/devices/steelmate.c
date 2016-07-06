/* Steelmate TPMS FSK protocol
 * Data packet is inverted Manchester encoded. It's also reversed MSB/LSB.
 *
 * Copyright © 2016 Benjamin Larsson
 * Copyright © 2016 John Jore
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

static int steelmate_callback(bitbuffer_t *bitbuffer) {
	if (debug_output >= 1) {
		fprintf(stdout, "Steelmate TPMS decoder\n");
		bitbuffer_print(bitbuffer);
		fprintf(stdout, "\n");
	}

	char time_str[LOCAL_TIME_BUFLEN];
	local_time_str(0, time_str);
	bitrow_t *bb = bitbuffer->bb;

	//Loop through each row of data
	for (int i = 0; i < bitbuffer->num_rows; i++)
	{
		//Length must be 72 bits to be considered a valid packet
		if (bitbuffer->bits_per_row[i] != 72)
			continue;

		if (debug_output >= 1) {
			fprintf(stdout, "Raw data: %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", bb[i][0], bb[i][1], bb[i][2], bb[i][3], bb[i][4], bb[i][5], bb[i][6], bb[i][7], bb[i][8]);
		}

		//Valid preamble? (Note, the data is still wrong order at this point. Correct pre-amble: 0x00 0x00 0x01)
		if (bb[i][0] != 0x00 || bb[i][1] != 0x00 || bb[i][2] != 0x7f)
			continue;

		//Payload is inverted Manchester encoded, and reversed MSB/LSB order

		//Preamble
		uint8_t preAmble = ~reverse8(bb[i][2]);

		//Sensor ID
		uint8_t ID1 = ~reverse8(bb[i][3]);
		uint8_t ID2 = ~reverse8(bb[i][4]);
		uint16_t sensorID = (ID1 << 8) + ID2;
		char sensorIDhex[7];
		sprintf(sensorIDhex, "0x%04x", sensorID);
		
		//Pressure is stored as twice the PSI
		uint8_t p1 = ~reverse8(bb[i][5]);
		double pressurePSI = (double)p1 / 2;

		//Temperature is stored in Fahrenheit. Note that the datasheet claims operational to -40'C, but can only express values from -17.8'C
		uint8_t tempFahrenheit = ~reverse8(bb[i][6]);
		double tempCelcius = (double)(tempFahrenheit - 32) * 5 / 9;

		//Battery voltage is stored as half the mV
		uint8_t tmpbattery_mV = ~reverse8(bb[i][7]);
		uint16_t battery_mV = tmpbattery_mV * 2;

		//Checksum is a sum of all the other values
		uint8_t payload_checksum = ~reverse8(bb[i][8]);
		uint8_t calculated_checksum = preAmble + ID1 + ID2 + p1 + tempFahrenheit + tmpbattery_mV;
		if (payload_checksum != calculated_checksum)
			continue;
		
		if (debug_output >= 1) {
			fprintf(stdout, "Sensor ID: %04x. Pressure: %f PSI. Temperature: %f 'C. Battery %i mV\n", sensorID, pressurePSI, tempCelcius, battery_mV);
		}

		data_t *data = data_make("time", "", DATA_STRING, time_str,
			"type", "", DATA_STRING, "TPMS",
			"make", "", DATA_STRING, "Steelmate",
			"id", "", DATA_STRING, sensorIDhex,
			"pressure_PSI", "", DATA_DOUBLE, pressurePSI,
			"temperature_C", "", DATA_DOUBLE, tempCelcius,
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
	"temperature_C",
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
