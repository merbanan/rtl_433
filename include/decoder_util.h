/**
 * High-level utility functions for decoders
 *
 * Copyright (C) 2018 Christian Zuckschwerdt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef INCLUDE_DECODER_UTIL_H_
#define INCLUDE_DECODER_UTIL_H_

#include <stdarg.h>
#include "bitbuffer.h"

// be terse, a maximum msg length of 60 characters is supported on the decoder_output_ functions
// e.g. "FoobarCorp-XY3000: unexpected type code %02x"

/// Output a message
void decoder_output_message(char const *msg);

/// Output a message and the content of a bitbuffer
void decoder_output_bitbuffer(bitbuffer_t const *bitbuffer, char const *msg);

/// Output a message and the content of a bitbuffer
/// Not recommended.
void decoder_output_bitbuffer_array(bitbuffer_t const *bitbuffer, char const *msg);

/// Output a message and the content of a bit row (byte buffer)
void decoder_output_bitrow(bitrow_t const bitrow, unsigned bit_len, char const *msg);

// print helpers

/// Output a message with args
void decoder_output_messagef(char const *restrict format, ...);

/// Output a message with args and the content of a bitbuffer
void decoder_output_bitbufferf(bitbuffer_t const *bitbuffer, char const *restrict format, ...);

/// Output a message with args and the content of a bitbuffer
void decoder_output_bitbuffer_arrayf(bitbuffer_t const *bitbuffer, char const *restrict format, ...);

/// Output a message with args and the content of a bit row (byte buffer)
void decoder_output_bitrowf(bitrow_t const bitrow, unsigned bit_len, char const *restrict format, ...);


/// Print the content of the bitbuffer
void bitbuffer_printf(const bitbuffer_t *bitbuffer, char const *restrict format, ...);

/// Debug the content of the bitbuffer
void bitbuffer_debugf(const bitbuffer_t *bitbuffer, char const *restrict format, ...);

/// Print the content of a bit row (byte buffer)
void bitrow_printf(bitrow_t const bitrow, unsigned bit_len, char const *restrict format, ...);

/// Debug the content of a bit row (byte buffer)
void bitrow_debugf(bitrow_t const bitrow, unsigned bit_len, char const *restrict format, ...);

#endif /* INCLUDE_DECODER_UTIL_H_ */
