/** @file
    High-level utility functions for decoders.

    Copyright (C) 2018 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdlib.h>
#include <stdio.h>
#include "data.h"
#include "util.h"
#include "decoder_util.h"

// create decoder functions

r_device *create_device(r_device *template)
{
    r_device *r_dev = malloc(sizeof (*r_dev));
    if (template)
        *r_dev = *template; // copy

    return r_dev;
}

// variadic print functions

void bitbuffer_printf(const bitbuffer_t *bitbuffer, char const *restrict format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    bitbuffer_print(bitbuffer);
}

void bitbuffer_debugf(const bitbuffer_t *bitbuffer, char const *restrict format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    bitbuffer_debug(bitbuffer);
}

void bitrow_printf(bitrow_t const bitrow, unsigned bit_len, char const *restrict format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    bitrow_print(bitrow, bit_len);
}

void bitrow_debugf(bitrow_t const bitrow, unsigned bit_len, char const *restrict format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    bitrow_debug(bitrow, bit_len);
}

// variadic output functions

void decoder_output_messagef(r_device *decoder, char const *restrict format, ...)
{
    char msg[60]; // fixed length limit
    va_list ap;
    va_start(ap, format);
    vsnprintf(msg, 60, format, ap);
    va_end(ap);
    decoder_output_message(decoder, msg);
}

void decoder_output_bitbufferf(r_device *decoder, bitbuffer_t const *bitbuffer, char const *restrict format, ...)
{
    char msg[60]; // fixed length limit
    va_list ap;
    va_start(ap, format);
    vsnprintf(msg, 60, format, ap);
    va_end(ap);
    decoder_output_bitbuffer(decoder, bitbuffer, msg);
}

void decoder_output_bitbuffer_arrayf(r_device *decoder, bitbuffer_t const *bitbuffer, char const *restrict format, ...)
{
    char msg[60]; // fixed length limit
    va_list ap;
    va_start(ap, format);
    vsnprintf(msg, 60, format, ap);
    va_end(ap);
    decoder_output_bitbuffer_array(decoder, bitbuffer, msg);
}

void decoder_output_bitrowf(r_device *decoder, bitrow_t const bitrow, unsigned bit_len, char const *restrict format, ...)
{
    char msg[60]; // fixed length limit
    va_list ap;
    va_start(ap, format);
    vsnprintf(msg, 60, format, ap);
    va_end(ap);
    decoder_output_bitrow(decoder, bitrow, bit_len, msg);
}

// output functions

void decoder_output_data(r_device *decoder, data_t *data)
{
    decoder->output_fn(decoder, data);
}

void decoder_output_message(r_device *decoder, char const *msg)
{
    data_t *data = data_make(
            "msg", "", DATA_STRING, msg,
            NULL);
    decoder_output_data(decoder, data);
}

static char *bitrow_print_bits(bitrow_t const bitrow, unsigned bit_len)
{
    char *row_bits, *p;

    p = row_bits = malloc(bit_len + bit_len / 4 + 1); // "1..\0" (1 space per nibble)

    // print bit-wide with a space every nibble
    for (unsigned i = 0; i < bit_len; ++i) {
        if (i > 0 && i % 4 == 0) {
            *p++ = ' ';
        }
        if (bitrow[i / 8] & (0x80 >> (i % 8))) {
            *p++ = '1';
        }
        else {
            *p++ = '0';
        }
    }
    *p++ = '\0';

    return row_bits;
}

void decoder_output_bitbuffer(r_device *decoder, bitbuffer_t const *bitbuffer, char const *msg)
{
    data_t *data;
    char *row_codes[BITBUF_ROWS];
    char *row_bits[BITBUF_ROWS] = {0};
    char row_bytes[BITBUF_COLS * 2 + 1];
    unsigned i;

    for (i = 0; i < bitbuffer->num_rows; i++) {
        row_bytes[0] = '\0';
        // print byte-wide
        for (unsigned col = 0; col < (unsigned)(bitbuffer->bits_per_row[i] + 7) / 8; ++col) {
            sprintf(&row_bytes[2 * col], "%02x", bitbuffer->bb[i][col]);
        }
        // remove last nibble if needed
        row_bytes[2 * (bitbuffer->bits_per_row[i] + 3) / 8] = '\0';

        // a simpler representation for csv output
        row_codes[i] = malloc(8 + BITBUF_COLS * 2 + 1); // "{nnn}..\0"
        sprintf(row_codes[i], "{%d}%s", bitbuffer->bits_per_row[i], row_bytes);

        if (decoder->verbose_bits) {
            row_bits[i] = bitrow_print_bits(bitbuffer->bb[i], bitbuffer->bits_per_row[i]);
        }
    }

    data = data_make(
            "msg", "", DATA_STRING, msg,
            "num_rows", "", DATA_INT, bitbuffer->num_rows,
            "codes", "", DATA_ARRAY, data_array(bitbuffer->num_rows, DATA_STRING, row_codes),
            NULL);

    if (decoder->verbose_bits) {
        data_append(data,
                "bits", "", DATA_ARRAY, data_array(bitbuffer->num_rows, DATA_STRING, row_bits),
                NULL);
    }

    decoder_output_data(decoder, data);

    for (i = 0; i < bitbuffer->num_rows; i++) {
        free(row_codes[i]);
        free(row_bits[i]);
    }
}

void decoder_output_bitbuffer_array(r_device *decoder, bitbuffer_t const *bitbuffer, char const *msg)
{
    data_t *data;
    data_t *row_data[BITBUF_ROWS];
    char *row_codes[BITBUF_ROWS];
    char row_bytes[BITBUF_COLS * 2 + 1];
    unsigned i;

    for (i = 0; i < bitbuffer->num_rows; i++) {
        row_bytes[0] = '\0';
        // print byte-wide
        for (unsigned col = 0; col < (unsigned)(bitbuffer->bits_per_row[i] + 7) / 8; ++col) {
            sprintf(&row_bytes[2 * col], "%02x", bitbuffer->bb[i][col]);
        }
        // remove last nibble if needed
        row_bytes[2 * (bitbuffer->bits_per_row[i] + 3) / 8] = '\0';

        row_data[i] = data_make(
                "len", "", DATA_INT, bitbuffer->bits_per_row[i],
                "data", "", DATA_STRING, row_bytes,
                NULL);

        // a simpler representation for csv output
        row_codes[i] = malloc(8 + BITBUF_COLS * 2 + 1); // "{nnn}..\0"
        sprintf(row_codes[i], "{%d}%s", bitbuffer->bits_per_row[i], row_bytes);
    }

    data = data_make(
            "msg", "", DATA_STRING, msg,
            "num_rows", "", DATA_INT, bitbuffer->num_rows,
            "rows", "", DATA_ARRAY, data_array(bitbuffer->num_rows, DATA_DATA, row_data),
            "codes", "", DATA_ARRAY, data_array(bitbuffer->num_rows, DATA_STRING, row_codes),
            NULL);
    decoder_output_data(decoder, data);

    for (i = 0; i < bitbuffer->num_rows; i++) {
        free(row_codes[i]);
    }
}

void decoder_output_bitrow(r_device *decoder, bitrow_t const bitrow, unsigned bit_len, char const *msg)
{
    data_t *data;
    char *row_code;
    char *row_bits = NULL;
    char row_bytes[BITBUF_COLS * 2 + 1];

    row_bytes[0] = '\0';
    // print byte-wide
    for (unsigned col = 0; col < (bit_len + 7) / 8; ++col) {
        sprintf(&row_bytes[2 * col], "%02x", bitrow[col]);
    }
    // remove last nibble if needed
    row_bytes[2 * (bit_len + 3) / 8] = '\0';

    // a simpler representation for csv output
    row_code = malloc(8 + BITBUF_COLS * 2 + 1); // "{nnn}..\0"
    sprintf(row_code, "{%d}%s", bit_len, row_bytes);

    data = data_make(
            "msg", "", DATA_STRING, msg,
            "codes", "", DATA_STRING, row_code,
            NULL);

    if (decoder->verbose_bits) {
        row_bits = bitrow_print_bits(bitrow, bit_len);
        data_append(data,
                "bits", "", DATA_STRING, row_bits,
                NULL);
    }

    decoder_output_data(decoder, data);

    free(row_code);
    free(row_bits);
}
