/** @file
    High-level utility functions for decoders.

    Copyright (C) 2018 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_DECODER_UTIL_H_
#define INCLUDE_DECODER_UTIL_H_

#include <stdarg.h>
#include "bitbuffer.h"
#include "r_device.h"

/// Create a new r_device, copy from template if not NULL.
r_device *create_device(r_device *template);

/// Output data.
void decoder_output_data(r_device *decoder, data_t *data);

// be terse, a maximum msg length of 60 characters is supported on the decoder_output_ functions
// e.g. "FoobarCorp-XY3000: unexpected type code %02x"

/// Output a message.
void decoder_output_message(r_device *decoder, char const *msg);

/// Output a message and the content of a bitbuffer.
void decoder_output_bitbuffer(r_device *decoder, bitbuffer_t const *bitbuffer, char const *msg);

/// Output a message and the content of a bitbuffer.
/// Usage not recommended.
void decoder_output_bitbuffer_array(r_device *decoder, bitbuffer_t const *bitbuffer, char const *msg);

/// Output a message and the content of a bit row (byte buffer).
void decoder_output_bitrow(r_device *decoder, bitrow_t const bitrow, unsigned bit_len, char const *msg);

// print helpers

/// Output a message with args.
void decoder_output_messagef(r_device *decoder, char const *restrict format, ...);

/// Output a message with args and the content of a bitbuffer.
void decoder_output_bitbufferf(r_device *decoder, bitbuffer_t const *bitbuffer, char const *restrict format, ...);

/// Output a message with args and the content of a bitbuffer.
void decoder_output_bitbuffer_arrayf(r_device *decoder, bitbuffer_t const *bitbuffer, char const *restrict format, ...);

/// Output a message with args and the content of a bit row (byte buffer).
void decoder_output_bitrowf(r_device *decoder, bitrow_t const bitrow, unsigned bit_len, char const *restrict format, ...);

/// Print the content of the bitbuffer.
void bitbuffer_printf(const bitbuffer_t *bitbuffer, char const *restrict format, ...);

/// Debug print the content of the bitbuffer.
/// For quick and easy debugging, not for regular usage.
void bitbuffer_debugf(const bitbuffer_t *bitbuffer, char const *restrict format, ...);

/// Print the content of a bit row (byte buffer).
void bitrow_printf(bitrow_t const bitrow, unsigned bit_len, char const *restrict format, ...);

/// Debug print the content of a bit row (byte buffer).
/// For quick and easy debugging, not for regular usage.
void bitrow_debugf(bitrow_t const bitrow, unsigned bit_len, char const *restrict format, ...);

#endif /* INCLUDE_DECODER_UTIL_H_ */
