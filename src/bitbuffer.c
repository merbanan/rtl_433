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
	bits->row_index = 0;
	bits->bit_col_index = 0;
	memset(bits->bits_per_row, 0, BITBUF_ROWS*2);
	memset(bits->bits_buffer, 0, BITBUF_ROWS * BITBUF_COLS);
}


void bitbuffer_add_bit(bitbuffer_t *bits, int bit) {
	uint16_t col_index = bits->bits_per_row[bits->row_index]/8;
	if((col_index < BITBUF_COLS)
	&& (bits->row_index < BITBUF_ROWS)
	) {
		bits->bits_buffer[bits->row_index][col_index] |= bit << (7-bits->bit_col_index);
		bits->bit_col_index++;
		bits->bit_col_index %= 8;		// Wrap around
		bits->bits_per_row[bits->row_index]++;
	}
	else {
		fprintf(stderr, "ERROR: bitbuffer:: Could not add more columns\n");
	}
}


void bitbuffer_add_row(bitbuffer_t *bits) {
	if(bits->row_index < BITBUF_ROWS) {
		bits->row_index++;
		bits->bit_col_index = 0;
	} 
	else {
		fprintf(stderr, "ERROR: bitbuffer:: Could not add more rows\n");
	}
}


void bitbuffer_print(const bitbuffer_t *bits) {
	fprintf(stderr, "bitbuffer:: row_index: %d, bit_col_index: %d\n", bits->row_index, bits->bit_col_index);
	for (int row = 0; row <= bits->row_index; ++row) {
		fprintf(stderr, "[%02d] {%d} ", row, bits->bits_per_row[row]);
		for (int col = 0; col < (bits->bits_per_row[row]+7)/8; ++col) {
			fprintf(stderr, "%02x ", bits->bits_buffer[row][col]);
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
