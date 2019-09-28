/* FSK 8 byte Manchester encoded TPMS with simple checksum.
 * Seen on Ford Fiesta, Focus, ...
 *
 * Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Packet nibbles:  IIIIIIII PP TT FF CC
 * I = ID
 * P = likely Pressure
 * T = likely Temperature
 * F = Flags, (46: 87% 1e: 5% 06: 2% 4b: 1% 66: 1% 0e: 1% 44: 1%)
 * C = Checksum, SUM bytes 0 to 6 = byte 7
 
 
 Modifying for TRW Hyundai Elantra
 Preamble is 111 0001 0101 0101 (0x7155)
 
 PPTT IDID IDID FFCC
 Pressure in hex(One byte PP) to dec+60 = pressure in kPa
 Temperature hex(One byte TT) to dec -50 = temp in C
 ID in hex(2 Words = 4 bytes)
 Flags (FF) = ???? ?SBT (Missing Acceleration, market - Europe/US/Asia, Tire type, Alert Mode, park mode, High Line vs Low LIne etc)
 S=Storage bit
 B=Battery low bit
 T=Triggered bit
 C0 =1100 0000 = Battery OK, Not Triggered
 C1 =1100 0001 = Battery OK, Triggered
 C2 =1100 0010 = Battery Low, Not Triggered
 C3 =1100 0011 = Battery Low, Triggered
 C5 =1100 0101 = Battery OK, Triggered, Storage Mode
 E1 =1110 0001 = Mx Sensor Clone for Elantra 2012 US market ? Low Line
 C1		 = Mx Sensor Clone for Genesis Sedan 2012 US market ? High Line
 
 CC = CRC8
 994A02226097C127
 (99 4A 02 22 60 97 C1)CRC8 = 27
 http://www.sunshine2k.de/coding/javascript/crc/crc_js.html
 024C801A2D39C197
 (024C801A2D39C1)CRC8 = 97
 
 Manchester decoded data
 0000000101001100000000110001011011101100111001101110000101010111 (64 bits)
 B[0]		b[1]		b[2]		b[3]		b[4]		b[5]		[b6]		b[7]	
 0000	0001 	0100 	1100 	0000	0011	0001	0110	1110	1100	1110	0110	1110	0001	0101	0111
 P	P	T	T	I	I	I	I	I	I	I	I	F	F	C	C
*/

#include "decoder.h"

/*preamble = 111000101010101 0x71 0x55, inverted = 000 111010101010 0x0e 0xaa */
//Full preamble mx sensor = 11011010111000101010101
// inverted 00100101000111010101010 12 8E aa
static const uint8_t preamble_pattern[2] = {0x71,0x55}; // 31 bits

static int tpms_ford_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    data_t *data;
    unsigned int start_pos;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    int id;
    char id_str[8];
    int code;
    char code_str[7];
	unsigned status, pressure1, pressure2, temp, battery_low, counter, failed;
	float pressure_kpa, temperature_c;
	int crc;

	/*unsigned bitbuffer_manchester_decode(bitbuffer_t *inbuf, unsigned row, unsigned start,
	 bitbuffer_t *outbuf, unsigned max)*/
	
    start_pos = bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 64);
    // require 64 data bits
	
	printf("manchester decode called");
	printf("start_pos=%d",start_pos);
	if (start_pos-bitpos < 128) {
		return 0;
	}
	unsigned z=start_pos-bitpos;
	printf("size=%d",z);
	b = packet_bits.bb[0];
	
	printf("byte 0=%d",b[0]);
	printf("byte 1=%d",b[1]);
	printf("byte 2=%d",b[2]);
	printf("byte 3=%d",b[3]);
	printf("byte 4=%d",b[4]);
	printf("byte 5=%d",b[5]);
	printf("byte 6=%d",b[6]);
	printf("byte 7=%d",b[7]);
	
	
   /* if (((b[0]+b[1]+b[2]+b[3]+b[4]+b[5]+b[6]) & 0xff) != b[7]) {
        return 0;
    }*/
	//uint8_t crc8(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init)
	//crc = b[7];
	//if (crc8(b, 7, 0x07, 0x00) != crc) {
	//	return 0;
	//}
	
	//printf("CRC passed");
    id = b[2]<<24 | b[3]<<16 | b[4]<<8 | b[5];
    //sprintf(id_str, "%08x", id);
	printf("%08x", id);

    data = data_make(
        "model",        "",     DATA_STRING, "Elantra",
        "type",         "",     DATA_STRING, "TPMS",
        "id",           "",     DATA_STRING, id_str,
        NULL);

    decoder_output_data(decoder, data);
    return 1;
}

static int tpms_ford_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    int row;
    unsigned bitpos;
    int events = 0;
	printf("main function called");
	//bitbuffer_invert(bitbuffer);
	//printf("inverted");
	
    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
		bitbuffer_print(bitbuffer);
		/*unsigned bitbuffer_search(bitbuffer_t *bitbuffer, unsigned row, unsigned start,
		 const uint8_t *pattern, unsigned pattern_bits_len)*/
		// Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                (const uint8_t *)&preamble_pattern, 16)) + 128 <=
                bitbuffer->bits_per_row[row]) {
			printf("bit_pos=%d",bitpos);
			//bitrow_t tmp;
			//bitbuffer_extract_bytes(bitbuffer, row, bitpos, tmp, len);
			bitbuffer_print(bitbuffer);
			events += tpms_ford_decode(decoder, bitbuffer, row, bitpos + 16);
            bitpos += 15;
        }
    }

    return events;
}

static char *output_fields[] = {
    "model",
    "type",
    "id",
    NULL
};

r_device tpms_ford = {
    .name           = "Elantra TPMS",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 49, // 12-13 samples @250k
    .long_width     = 49, // FSK
    .reset_limit    = 50000, // Maximum gap size before End Of Message [us].
    .decode_fn      = &tpms_ford_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
