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

#ifndef INCLUDE_BITBUFFER_H_
#define INCLUDE_BITBUFFER_H_

#include <stdint.h>

#define BITBUF_COLS		34		// Number of bytes in a column
#define BITBUF_ROWS		50
#define BITBUF_MAX_PRINT_BITS	50	// Maximum number of bits to print (in addition to hex values)

/// Bit buffer
typedef struct {
	int		row_index;		// Number of active rows - 1
	int		bit_col_index;	// Bit index into byte (0 is MSB, 7 is LSB)
	int16_t bits_per_row[BITBUF_ROWS];
	uint8_t bits_buffer[BITBUF_ROWS][BITBUF_COLS];
} bitbuffer_t;


/// Clear the content of the bitbuffer
void bitbuffer_clear(bitbuffer_t *bits);

/// Add a single bit at the end of the bitbuffer (MSB first)
void bitbuffer_add_bit(bitbuffer_t *bits, int bit);

/// Add a new row to the bitbuffer
void bitbuffer_add_row(bitbuffer_t *bits);

/// Invert all bits in the bitbuffer (do not invert the empty bits)
//void bitbuffer_invert(bitbuffer_t *bits);

/// Print the content of the bitbuffer
void bitbuffer_print(const bitbuffer_t *bits);


#endif /* INCLUDE_BITBUFFER_H_ */
