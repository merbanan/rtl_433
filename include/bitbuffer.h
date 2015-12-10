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

#define BITBUF_COLS		80		// Number of bytes in a column
#define BITBUF_ROWS		25
#define BITBUF_MAX_PRINT_BITS	50	// Maximum number of bits to print (in addition to hex values)

typedef uint8_t bitrow_t[BITBUF_COLS];
typedef bitrow_t bitarray_t[BITBUF_ROWS];

/// Bit buffer
typedef struct {
	uint16_t	num_rows;	// Number of active rows
	uint16_t	bits_per_row[BITBUF_ROWS];	// Number of active bits per row
	bitarray_t	bb;			// The actual bits buffer
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

// Search the specified row of the bitbuffer, starting from bit 'start', for
// the pattern provided. Return the location of the first match, or the end
// of the row if no match is found.
// The pattern starts in the high bit. For example if searching for 011011
// the byte pointed to by 'pattern' would be 0xAC. (011011xx).
unsigned bitbuffer_search(bitbuffer_t *bitbuffer, unsigned row, unsigned start,
			  const uint8_t *pattern, unsigned pattern_bits_len);

// Manchester decoding from one bitbuffer into another, starting at the
// specified row and start bit. Decode at most 'max' data bits (i.e. 2*max)
// bits from the input buffer). Return the bit position in the input row
// (i.e. returns start + 2*outbuf->bits_per_row[0]).
unsigned bitbuffer_manchester_decode(bitbuffer_t *inbuf, unsigned row, unsigned start,
				     bitbuffer_t *outbuf, unsigned max);

#endif /* INCLUDE_BITBUFFER_H_ */
