/**
 * Bit buffer
 * 
 * A two-dimensional bit buffer consisting of bytes
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bitbuffer.h"
#include <stdio.h>
#include <string.h>


void bitbuffer_clear(bitbuffer_t *bits) {
	bits->num_rows = 0;
	memset(bits->bits_per_row, 0, BITBUF_ROWS*2);
	memset(bits->bb, 0, BITBUF_ROWS * BITBUF_COLS);
}


void bitbuffer_add_bit(bitbuffer_t *bits, int bit) {
	if(bits->num_rows == 0) bits->num_rows++;	// Add first row automatically
	uint16_t col_index = bits->bits_per_row[bits->num_rows-1] / 8;
	uint16_t bit_index = bits->bits_per_row[bits->num_rows-1] % 8;
	if((col_index < BITBUF_COLS)
	&& (bits->num_rows <= BITBUF_ROWS)
	) {
		bits->bb[bits->num_rows-1][col_index] |= (bit << (7-bit_index));
		bits->bits_per_row[bits->num_rows-1]++;
	}
	else {
//		fprintf(stderr, "ERROR: bitbuffer:: Could not add more columns\n");	// Some decoders may add many columns...
	}
}


void bitbuffer_add_row(bitbuffer_t *bits) {
	if(bits->num_rows == 0) bits->num_rows++;	// Add first row automatically
	if((bits->bits_per_row[bits->num_rows-1] > 0) && (bits->num_rows < BITBUF_ROWS)) {  // No empty rows
		bits->num_rows++;
	}
	else {
//		fprintf(stderr, "ERROR: bitbuffer:: Could not add more rows\n");	// Some decoders may add many rows...
	}
}

// If we make this an inline function instead of a macro, it means we don't
// have to worry about using bit numbers with side-effects (bit++).
static inline int bit(const uint8_t *bytes, unsigned bit)
{
	return bytes[bit >> 3] >> (7 - (bit & 7)) & 1;
}

unsigned bitbuffer_search(bitbuffer_t *bitbuffer, unsigned row, unsigned start,
			  const uint8_t *pattern, unsigned pattern_bits_len)
{
    uint8_t *bits = bitbuffer->bb[row];
    unsigned len = bitbuffer->bits_per_row[row];
    unsigned ipos = start;
    unsigned ppos = 0;  // cursor on init pattern

    while (ipos < len && ppos < pattern_bits_len) {
	    if (bit(bits, ipos) == bit(pattern, ppos)) {
		    ppos++;
		    ipos++;
		    if (ppos == pattern_bits_len)
			    return ipos - pattern_bits_len;
	    } else {
		    ipos += -ppos + 1;
		    ppos = 0;
	    }
    }

    // Not found
    return len;
}

unsigned bitbuffer_manchester_decode(bitbuffer_t *inbuf, unsigned row, unsigned start,
				     bitbuffer_t *outbuf, unsigned max)
{
	uint8_t *bits = inbuf->bb[row];
	unsigned int len = inbuf->bits_per_row[row];
	unsigned int ipos = start;

	if (max && len > start + (max * 2))
		len = start + (max * 2);

	while (ipos < len) {
		uint8_t bit1, bit2;

		bit1 = bit(bits, ipos++);
		bit2 = bit(bits, ipos++);

		if (bit1 == bit2)
			break;

		bitbuffer_add_bit(outbuf, bit2);
	}

	return ipos;
}


void bitbuffer_print(const bitbuffer_t *bits) {
	fprintf(stderr, "bitbuffer:: Number of rows: %d \n", bits->num_rows);
	for (uint16_t row = 0; row < bits->num_rows; ++row) {
		fprintf(stderr, "[%02d] {%d} ", row, bits->bits_per_row[row]);
		for (uint16_t col = 0; col < (bits->bits_per_row[row]+7)/8; ++col) {
			fprintf(stderr, "%02x ", bits->bb[row][col]);
		}
		// Print binary values also?
		if (bits->bits_per_row[row] <= BITBUF_MAX_PRINT_BITS) {
			fprintf(stderr, ": ");
			for (uint16_t bit = 0; bit < bits->bits_per_row[row]; ++bit) {
				if (bits->bb[row][bit/8] & (0x80 >> (bit % 8))) {
					fprintf(stderr, "1");
				} else {
					fprintf(stderr, "0");
				}
				if ((bit % 8) == 7) { fprintf(stderr, " "); }	// Add byte separators
			}
		}
		fprintf(stderr, "\n");
	}
}


// Test code
// gcc -I include/ -std=gnu11 -D _TEST src/bitbuffer.c
#ifdef _TEST
int main(int argc, char **argv) {
	fprintf(stderr, "bitbuffer:: test\n");

	bitbuffer_t bits = {0};

	fprintf(stderr, "TEST: bitbuffer:: The empty buffer\n");
	bitbuffer_print(&bits);
	
	fprintf(stderr, "TEST: bitbuffer:: Add 1 bit\n");
	bitbuffer_add_bit(&bits, 1);
	bitbuffer_print(&bits);

	fprintf(stderr, "TEST: bitbuffer:: Add 1 new row\n");
	bitbuffer_add_row(&bits);
	bitbuffer_print(&bits);

	fprintf(stderr, "TEST: bitbuffer:: Fill row\n");
	for (int i=0; i < BITBUF_COLS*8; ++i) {
		bitbuffer_add_bit(&bits, i%2);
	}
	bitbuffer_print(&bits);

	fprintf(stderr, "TEST: bitbuffer:: Add row and fill 1 column too many\n");
	bitbuffer_add_row(&bits);
	for (int i=0; i <= BITBUF_COLS*8; ++i) {
		bitbuffer_add_bit(&bits, i%2);
	}
	bitbuffer_print(&bits);

	fprintf(stderr, "TEST: bitbuffer:: Clear\n");
	bitbuffer_clear(&bits);
	bitbuffer_print(&bits);

	fprintf(stderr, "TEST: bitbuffer:: Add 1 row too many\n");
	for (int i=0; i <= BITBUF_ROWS; ++i) {
		bitbuffer_add_row(&bits);
	}
	bitbuffer_add_bit(&bits, 1);
	bitbuffer_print(&bits);

	return 0;
}
#endif /* _TEST */
