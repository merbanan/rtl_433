/**
	Hyundai TPMS (VDO) FSK 9 or 10 byte Manchester encoded checksummed TPMS data.

	Copyright (C) 2020 Todor Uzunov aka teou, TTiges, 2019 Andreas Spiess, 2017 Christian W. Zuckschwerdt <zany@triq.net>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
*/

/**
Tested on a Hyundai i30 PDE. It uses sensors from Continental/VDO. VDO reference/part no.: A2C98607702, generation TG1C, FCC ID: KR5TIS-01
Similar sensors and probably protocol are used in models from BMW, Fiat-Chrysler-Alfa, Peugeot-Citroen, Hyundai-KIA, Mitsubishi, Mazda, etc.
https://www.vdo.com/media/746526/2019-10_tpms-oe-sensors_application-list.pdf
Hence my compilation and fine-tuning the already present as of late 2020 source for Citroen, Abarth and Jansite.

Working Temperature: -50°C to 125°C (but according to some sources the chip can only handle -40°C)
Working Frequency: 433.92MHz+-38KHz
Tire monitoring range value: 0kPa-350kPa+-7kPa


Packet nibbles:

PRE    UU  IIIIIIII FR  PP TT BB  CC

- PRE = preamble is 55 55 55 56 (inverted: aa aa aa a9)
- U = state, decoding unknown, not included in checksum. In all tests has values 20,21,22,23 in hex.
Probably codes the information how was the sensor activated - external service tool or internal accelometer (first byte) and the speed of transmission?
- I = sensor Id in hex
- F = Flags, in my tests always 0. Most probably here are coded in separate bits the alert flags for low pressure (below 150kPa), temperature and low battery.
So it should be something like 00 (0) - all ok, 01 (1) - low pressure, 11 (3) - low pressure and low battery and so on, but more testing is necessary
- R = packet Repetition in every burst (about 10-11 identical packets are transmitted in every burst approximately once per 64 seconds)
- P = Pressure X/5=PSI or X(dec).1.375=kPa
- T = Temperature (deg C offset by 50)
- B = Battery and/or acceleration??
- C = probably Checksum, differs even between packets in one burst, i could not figure it out and left it out, hence the sanity checks and full preamble
*/

#include "decoder.h"

static int tpms_hyundai_vdo_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
	data_t *data;
	bitbuffer_t packet_bits = {0};
	uint8_t *b;
	int state;
	char state_str[3];
	unsigned id;
	char id_str[9 + 1];
	int flags;
	int repeat;
	int pressure;
	int temperature;
	char bat_acc[3];
//	char code_str[9 * 2 + 1];

	bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 72);

	if (packet_bits.bits_per_row[0] < 72) {
		return DECODE_FAIL_SANITY; // too short to be a whole packet
	}

	b = packet_bits.bb[0];

	if (b[6] == 0 || b[7] == 0) {
		return DECODE_ABORT_EARLY; // pressure cannot really be 0, temperature is also probably not -50C
	}

	state = b[0]; // not covered by CRC
	sprintf(state_str, "%02x", state);
	id = (unsigned)b[1] << 24 | b[2] << 16 | b[3] << 8 | b[4];
	sprintf(id_str, "%08x", id);
	flags = b[5]>>4;
	repeat = b[5]&0x0f;
	pressure = b[6];
	temperature = b[7];
//	sprintf(code_str, "%02x%02x%02x%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8]);
	sprintf(bat_acc, "%02x", b[8]);

	data = data_make(
	"model",			"",				DATA_STRING, "Hyundai VDO",
	"type",				"",				DATA_STRING, "TPMS",
	"state",			"",				DATA_STRING, state_str,
	"id",				"",				DATA_STRING, id_str,
	"flags",			"",				DATA_INT, flags,
	"repeat",			"repetition",	DATA_INT, repeat,
	"pressure_kPa",		"pressure",		DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure * 1.375,
	"temperature_C",	"temp",			DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature - 50.0,
	"bat_acc", 			"bat_or_acc",	DATA_STRING, bat_acc,
//	"code",				"raw_hex",		DATA_STRING, code_str, // the whole packet in hex for debugging ot testing
	NULL);

	decoder_output_data(decoder, data);
	return 1;
}

static int tpms_hyundai_vdo_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
//	full preamble is 55 55 55 56 (inverted: aa aa aa a9)
	uint8_t const preamble_pattern[4] = {0xaa, 0xaa, 0xaa, 0xa9};

	unsigned bitpos = 0;
	int ret         = 0;
	int events      = 0;

	bitbuffer_invert(bitbuffer);

//	Find a preamble with enough bits after it that it could be a complete packet
	while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 32)) + 72 <=
			bitbuffer->bits_per_row[0]) {
		ret = tpms_hyundai_vdo_decode(decoder, bitbuffer, 0, bitpos + 32);
		if (ret > 0)
			events += ret;
		bitpos += 2;
	}

	return events > 0 ? events : ret;
}

static char *output_fields[] = {
	"model",
	"type",
	"state",
	"id",
	"flags",
	"repeat",
	"pressure_kPa",
	"temperature_C",
	"bat_acc",
//	"code",
	NULL,
};

r_device tpms_hyundai_vdo = {
	.name        = "Hyundai TPMS (VDO)",
	.modulation  = FSK_PULSE_PCM,
	.short_width = 52,  // in the FCC test protocol is actually 42us, but works with 52 also
	.long_width  = 52,  // FSK
	.reset_limit = 150, // Maximum gap size before End Of Message [us].
	.decode_fn   = &tpms_hyundai_vdo_callback,
	.disabled    = 0,
	.fields      = output_fields,
};
