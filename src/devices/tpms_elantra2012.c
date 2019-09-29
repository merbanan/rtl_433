/* FSK 8 byte Manchester encoded TPMS with CRC8 checksum.
* Seen on Hyundai Elantra, Honda Civic ...
*
* Copyright (C) 2019 Kumar Vivek <kv2000in@gmail.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.

* TRW TPMS sensor FCC id GQ4-44T
* Mode/Sensor status -shipping, test,parking, driving, first block mode
* Battery voltage-Ok, low
* Trigger information- LF initiate TM
* Pressure- 1.4kPa
* temperature-27 deg C
* acceleration- 0.5 g
* Market - EU, America
* Tire type - 450 kPa
* Response time -8.14 seconds
* ID-8 bytes

* Preamble is 111 0001 0101 0101 (0x7155)

* PPTT IDID IDID FFCC
* Pressure in hex(One byte PP) to decimal+60 = pressure in kPa
* Temperature hex(One byte TT) to decimal-50 = temp in C
* ID in hex(2 Words = 4 bytes)
* Flags (FF) = ???? ?SBT (Missing Acceleration, market - Europe/US/Asia, Tire type, Alert Mode, park mode, High Line vs Low LIne etc)
* S=Storage bit
* B=Battery low bit
* T=Triggered bit
* C0 =1100 0000 = Battery OK, Not Triggered
* C1 =1100 0001 = Battery OK, Triggered
* C2 =1100 0010 = Battery Low, Not Triggered
* C3 =1100 0011 = Battery Low, Triggered
* C5 =1100 0101 = Battery OK, Triggered, Storage Mode
* E1 =1110 0001 = Mx Sensor Clone for Elantra 2012 US market ? Low Line
* C1		 = Mx Sensor Clone for Genesis Sedan 2012 US market ? High Line

* CC = CRC8
* 994A02226097C127
* (99 4A 02 22 60 97 C1)CRC8 = 27
* 024C801A2D39C197
* (024C801A2D39C1)CRC8 = 97

* Manchester decoded data
* 0000000101001100000000110001011011101100111001101110000101010111 (64 bits)
* B[0]		b[1]		b[2]		b[3]		b[4]		b[5]		[b6]		b[7]	
* 0000	0001 	0100 	1100 	0000	0011	0001	0110	1110	1100	1110	0110	1110	0001	0101	0111
* P	P	T	T	I	I	I	I	I	I	I	I	F	F	C	C
*/

#include "decoder.h"

/*preamble = 111000101010101 0x71 0x55*/
static const uint8_t preamble_pattern[2] = {0x71,0x55}; // 16 bits

static int tpms_elantra2012_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
data_t *data;
unsigned int start_pos;
bitbuffer_t packet_bits = {0};
uint8_t *b;
int id;
char id_str[16];
int code;
char code_str[16];
unsigned lf_triggered,battery_low, storage;
float pressure_kpa, temperature_c,pressure_psi;
int crc;
start_pos = bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 64);
// require 64 data bits
if (start_pos-bitpos < 128) {
return 0;
}
b = packet_bits.bb[0];
crc = b[7];
if (crc8(b, 7, 0x07, 0x00) != crc) {
return 0;
}

id = b[2]<<24 | b[3]<<16 | b[4]<<8 | b[5];
sprintf(id_str, "%08x", id);
code=b[6];
sprintf(code_str, "%x", code);
pressure_kpa=b[0]+60;
pressure_psi=pressure_kpa*0.14503779473358;
temperature_c=b[1]-50;
storage=(b[6] & (1<<2));//3rd bit
battery_low=(b[6] & (1<<1));//2nd bit
lf_triggered=(b[6] & (1<<0));//1st bit


data = data_make(
"model",        "",     DATA_STRING, 	"Elantra2012",
"type",         "",     DATA_STRING, 	"TPMS",
"id",           "",     DATA_STRING, 	id_str,
"pressure_kpa", "",		DATA_DOUBLE, 	pressure_kpa,
"pressure_psi", "",		DATA_DOUBLE, 	pressure_psi,			 
"temperature_c","",		DATA_DOUBLE, 	temperature_c,
"battery_low",	"",		DATA_INT,		battery_low,
"LF_triggered",	"",		DATA_INT,		lf_triggered,
"Storage",		"",		DATA_INT,		storage,
"Flags",		"",		DATA_STRING,	code_str,
NULL);

decoder_output_data(decoder, data);
return 1;
}

static int tpms_elantra2012_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
int row;
unsigned bitpos;
int events = 0;

for (row = 0; row < bitbuffer->num_rows; ++row) {
bitpos = 0;
// Find a preamble with enough bits after it that it could be a complete packet
while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
(const uint8_t *)&preamble_pattern, 16)) + 128 <=
bitbuffer->bits_per_row[row]) {
events += tpms_elantra2012_decode(decoder, bitbuffer, row, bitpos + 16);
bitpos += 15;
}
}

return events;
}

static char *output_fields[] = {
"model",
"type",
"id",
"pressure_kpa",
"pressure_psi",
"temperature_c",
"battery_low",
"LF_triggered",
"Storage",
"Flags",
NULL
};

r_device tpms_elantra2012 = {
.name           = "Elantra2012 TPMS",
.modulation     = FSK_PULSE_PCM,
.short_width    = 49, // 12-13 samples @1024k
.long_width     = 49, // FSK
.reset_limit    = 50000, // Maximum gap size before End Of Message [us].
.decode_fn      = &tpms_elantra2012_callback,
.disabled       = 0,
.fields         = output_fields,
};

