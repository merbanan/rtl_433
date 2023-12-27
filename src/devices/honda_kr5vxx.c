/** @file
    Honda Car Key FCCID KR5V2X and KR5V1X

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/

/**
	Decoder for Honda Car Keyfob
	FCCID KR5V2X
	Frequency 433.66 MHz and 434.18 MHz
	
	FCCID KR5V1X
	Frequency 313.55 MHz and 314.15 MHz

        Signal is 2FSK, 15 kHz deviation, datarate(baud) 16.66 kbps
*/



#include "decoder.h"

static int honda_decode(r_device *decoder, bitbuffer_t *bitbuffer){

	if( bitbuffer->bits_per_row[0] < 150 || bitbuffer->bits_per_row[0] > 184 ){ return DECODE_ABORT_EARLY; } //honda signals are usually 182 bits

	uint16_t bit_offset;
        uint8_t const preamble[] = {0xEC, 0x0F, 0x62};  //honda keyfob manufacture code
	bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8);
	if( bit_offset >= bitbuffer->bits_per_row[0] ||
	    (bit_offset + 136) > bitbuffer->bits_per_row[0] ){ return DECODE_ABORT_EARLY; }


	uint8_t hondacode[16];
	bitbuffer_extract_bytes( bitbuffer, 0, bit_offset + 16, hondacode, 120); //extract 120 bits starting at position ~45
	
	char const *car_command = "Unknown";
	char const *command_half = "Unknown";
	int keyfob_id = ((unsigned)hondacode[2] << 24) | (hondacode[3] << 16) | (hondacode[4] << 8) | (hondacode[5]);
	int keyfob_counter = (hondacode[7] << 16) | (hondacode[8] << 8) | (hondacode[9]);
	int rolling_code = ((unsigned)hondacode[10] << 24) | (hondacode[11] << 16) | (hondacode[12] << 8) | (hondacode[13]);
	
	switch( hondacode[6] ){
		case 0x21:
			car_command = "Lock(0x21)";
			break;
		case 0x22:
			car_command = "Unlock(0x22)";
			break;
		case 0x24:
			car_command = "Trunk(0x24)";
			break;
		case 0x2d:
			car_command = "RemoteStart(0x2D)";
			break;
		case 0x27:
			car_command = "Emergency(0x27)";
			break;
	}

	switch( hondacode[1] ){
		case 0x08:
			command_half = "1st(0x08)";
			break;
		case 0x0a:
			command_half = "2nd(0x0a)";
			break;
	}

	char raw_code[34];
	char *ptr = &raw_code[0];

	for(int i=0; i<15; i++){
		ptr += sprintf(ptr, "%02X", hondacode[i]);
	}

	int crc_value = crc8(hondacode, 14, 0x2f, 0x00); //OPENSAFETY-CRC8 uses polynomial 0x2F and init 0x00
	if( crc_value != (hondacode[14])){
			decoder_log(decoder, 1, __func__, "CRC error");
			return DECODE_FAIL_MIC;
	}

	data_t *data = data_make(
			"model",        "",      	DATA_STRING, "Honda Keyfob",
			"raw_code",	"RawCode",	DATA_STRING, raw_code,
			"keyfob_id",	"KeyfobID",	DATA_FORMAT, "%08x", DATA_INT, keyfob_id,
			"car_command",	"CarCommand",	DATA_STRING, car_command,
			"command_half",	"CommandHalf",	DATA_STRING, command_half,
			"keyfob_counter","FobCounter",	DATA_FORMAT, "%06x", DATA_INT, keyfob_counter,
			"rolling_code",	"RollCode",	DATA_FORMAT, "%08x", DATA_INT, rolling_code,
			"crc_value",	"CRCvalue",	DATA_FORMAT, "%02x", DATA_INT, crc_value,
			"mic",		"Integrity",	DATA_STRING, "CRC",
			NULL);

	decoder_output_data(decoder, data);
	return 1;
}


static char const *const output_fields[] = {
	"model",
	"raw_code",
	"keyfob_id",
	"car_command",
	"command_half",
	"keyfob_counter",
	"rolling_code",
	"crc_value",
	"mic",
	NULL,
};


r_device const honda_keyfob = {
	.name        = "Honda keyfob",
	.modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
	.short_width = 60,
	.long_width  = 124,
	.gap_limit   = 1000,  //this gap is kinda irrelevant
	.reset_limit = 75000,
	.decode_fn   = &honda_decode,
	.fields      = output_fields,
};
