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
	Frequency 433.66 MHz or 434.18 MHz
	
	FCCID KR5V1X
	Frequency 313.55 MHz or 314.15 MHz

	Signal is 2FSK, 15 kHz deviation, datarate(baud) 16.66 kbps
 	Device uses Manchester encoded pulses of 60 us and 120 us
  	Data packet starts with sync of 0xFFFFFFFFFFF
  	Data layout after sync:
           MMMMMM HH DDDDDDDD EE NNNNNN RRRRRRRR CC

 	- M: 24 bit Manufacturer ID
  	- H: 8 bit indicator of packet number (keyfob button press sends packet 2 times, reciever requires both packets. 0x08 is first packet, 0x0a is second packet)
   	- D: 32 bit Device ID of keyfob
    	- E: 8 bit Keyfob command (event)
     	- N: 24 bit counter
      	- R: 32 bit Rolling Code
       	- C: 8 bit CRC, OPENSAFETY poly 0x2f init 0x00

 	Flex decoder: rtl_433 -f 433657000 -R 0 -X 'n=honda,m=FSK_MC_ZEROBIT,s=60,l=120,r=75000,preamble={32}0xffffec0f'
  
*/



#include "decoder.h"

static int honda_decode(r_device *decoder, bitbuffer_t *bitbuffer){

	if (bitbuffer->num_rows > 1){ return DECODE_ABORT_EARLY; } //should only be 1 row

	if( bitbuffer->bits_per_row[0] < 150 || bitbuffer->bits_per_row[0] > 184 ){ return DECODE_ABORT_EARLY; }

	uint16_t bit_offset;
        uint8_t const preamble[] = {0xEC, 0x0F, 0x62};  //honda keyfob manufacture code
	bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8);
	if( bit_offset >= bitbuffer->bits_per_row[0] ||
	    (bit_offset + 136) > bitbuffer->bits_per_row[0] ){ return DECODE_ABORT_EARLY; }


	uint8_t b[16];
	bitbuffer_extract_bytes( bitbuffer, 0, bit_offset + 16, b, 120); //extract 120 bits, excluding first 2 bytes of manufacture code
	
	char const *event = "Unknown";
	int device_id = ((unsigned)b[2] << 24) | (b[3] << 16) | (b[4] << 8) | (b[5]);
	int device_counter = (b[7] << 16) | (b[8] << 8) | (b[9]);  //keyfob counter hex value
	int rolling_code = ((unsigned)b[10] << 24) | (b[11] << 16) | (b[12] << 8) | (b[13]);
	
	switch( b[6] ){
		case 0x21:
			event = "Lock";  //value 0x21
			break;
		case 0x22:
			event = "Unlock";  //value 0x22
			break;
		case 0x24:
			event = "Trunk";  //value 0x24
			break;
		case 0x2d:
			event = "RemoteStart"; //value 0x2D
			break;
		case 0x27:
			event = "Emergency"; //value 0x27
			break;
	}


	int crc_value = crc8(b, 14, 0x2f, 0x00); //OPENSAFETY-CRC8 uses polynomial 0x2F and init 0x00
	if( crc_value != (b[14])){
			decoder_log(decoder, 1, __func__, "CRC error");
			return DECODE_FAIL_MIC;
	}

	data_t *data = data_make(
			"model",        "",      	DATA_STRING, "Honda Keyfob",
			"id",	"Device ID",	DATA_FORMAT, "%08x", DATA_INT, device_id,
			"event",	"Event",	DATA_STRING, event,
			"counter","Counter",	DATA_FORMAT, "%06x", DATA_INT, device_counter,
			"code",	"Code",	DATA_FORMAT, "%08x", DATA_INT, rolling_code,
			"mic",		"Integrity",	DATA_STRING, "CRC",
			NULL);

	decoder_output_data(decoder, data);
	return 1;
}


static char const *const output_fields[] = {
	"model",
	"id",
	"event",
	"counter",
	"code",
	"mic",
	NULL,
};


r_device const honda_keyfob = {
	.name        = "Honda Keyfob",
	.modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
	.short_width = 60,
	.long_width  = 120,
	.gap_limit   = 1000,  //this gap is kinda irrelevant
	.reset_limit = 75000,
	.decode_fn   = &honda_decode,
	.fields      = output_fields,
};
