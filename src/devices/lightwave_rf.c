/* LightwaveRF protocol
 *
 * Stub for decoding test data only
 *
 * Reference: https://wiki.somakeit.org.uk/wiki/LightwaveRF_RF_Protocol
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"


/// Decode a nibble from byte value
/// Will return -1 if invalid byte is input
int lightwave_rf_nibble_from_byte(uint8_t in) {
	int nibble = -1;	// Default error
	switch(in) {
		case 0xF6:	nibble = 0x0;	break;
		case 0xEE:	nibble = 0x1;	break;
		case 0xED:	nibble = 0x2;	break;
		case 0xEB:	nibble = 0x3;	break;
		case 0xDE:	nibble = 0x4;	break;
		case 0xDD:	nibble = 0x5;	break;
		case 0xDB:	nibble = 0x6;	break;
		case 0xBE:	nibble = 0x7;	break;
		case 0xBD:	nibble = 0x8;	break;
		case 0xBB:	nibble = 0x9;	break;
		case 0xB7:	nibble = 0xA;	break;
		case 0x7E:	nibble = 0xB;	break;
		case 0x7D:	nibble = 0xC;	break;
		case 0x7B:	nibble = 0xD;	break;
		case 0x77:	nibble = 0xE;	break;
		case 0x6F:	nibble = 0xF;	break;
		// default:	// Just return error
	}
	return nibble;
}


static int lightwave_rf_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;

	// Validate package
	// Transmitted pulses are always 72
	// Pulse 72 (delimiting "1" is not demodulated, as gap becomes End-Of-Message - thus expected length is 71
	if ((bitbuffer->bits_per_row[0] == 71)
		&& (bitbuffer->num_rows == 1))		// There should be only one message (and we use the rest...) 
	{
		// Polarity is inverted
		bitbuffer_invert(bitbuffer);

		// Expand all "0" to "10" (bit stuffing)
		// row_in = 0, row_out = 1
		bitbuffer_add_row(bitbuffer);
		for (unsigned n=0; n < bitbuffer->bits_per_row[0]; ++n) {
			if (bitrow_get_bit(bb[0], n)) {
				bitbuffer_add_bit(bitbuffer, 1);
			} else {
				bitbuffer_add_bit(bitbuffer, 1);
				bitbuffer_add_bit(bitbuffer, 0);
			}
		}

		// Check length is correct
		// Due to encoding there will be two "0"s per byte, thus message grows to 91 bits
		if (bitbuffer->bits_per_row[1] != 91)	return 0;

		// Check initial delimiter bit is "1"
		unsigned bit_idx = 0;
		uint8_t delimiter_bit = bitrow_get_bit(bb[1], bit_idx++);
		if (delimiter_bit == 0)	return 0;	// Decode error

		// Strip delimiter bits
		// row_in = 1, row_out = 2
		bitbuffer_add_row(bitbuffer);
		for(unsigned n=0; n<10; ++n) {		// We have 10 bytes
			delimiter_bit = bitrow_get_bit(bb[1], bit_idx++);
			if (delimiter_bit == 0)	return 0;	// Decode error

			for(unsigned m=0; m<8; ++m) {
				bitbuffer_add_bit(bitbuffer, bitrow_get_bit(bb[1], bit_idx++));
			}
		}
		// Final delimiter bit will be missing - so do not check...

		// Decode bytes to nibbles
		// row_in = 2, row_out = 3
		bitbuffer_add_row(bitbuffer);
		for(unsigned n=0; n<10; ++n) {		// We have 10 bytes/nibbles
			int nibble = lightwave_rf_nibble_from_byte(bb[2][n]);
			if (nibble < 0) {
				if (debug_output) {
					fprintf(stderr, "LightwaveRF. Nibble decode error %X, idx: %u\n", bb[2][n], n);
					bitbuffer_print(bitbuffer);
				}
				return 0;	// Decode error
			}
			for (unsigned m=0; m<4; ++m) {	// Add nibble one bit at a time...
				bitbuffer_add_bit(bitbuffer, (nibble & (8 >> m)) >> (3-m));
			}
		}

		// Print out generic decode
		// Decoded nibbles are in row 3
		fprintf(stdout, "LightwaveRF:\n");
		fprintf(stdout, "ID = 0x%X%X%X\n", bb[3][2], bb[3][3], bb[3][4]);
		fprintf(stdout, "Subunit = %u\n", (bb[3][1] & 0xF0) >> 4);
		fprintf(stdout, "Command = %u\n", bb[3][1] & 0x0F);
		fprintf(stdout, "Parameter = %u\n", bb[3][0]);

		if (debug_output) {
			bitbuffer_print(bitbuffer);
			fprintf(stderr, "  Row 0 = Input, Row 1 = Zero bit stuffing, Row 2 = Stripped delimiters, Row 3 = Decoded nibbles\n");
		}


		return 1;
	}
	return 0;
}


r_device lightwave_rf = {
	.name			= "LightwaveRF",
	.modulation		= OOK_PULSE_PPM_RAW,
	.short_limit	= 750,	// Short gap 250µs, long gap 1250µs, (Pulse width is 250µs)
	.long_limit		= 1500,	//
	.reset_limit	= 1500, // Gap between messages is unknown so let us get them individually
	.json_callback	= &lightwave_rf_callback,
	.disabled		= 1,
	.demod_arg		= 0,
};



