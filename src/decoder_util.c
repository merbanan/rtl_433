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

#include <stdio.h>
//#include "bitbuffer.h"
//#include "data.h"
//#include "util.h"
#include "decoder.h"
#include "decoder_util.h"

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

void decoder_output_messagef(char const *restrict format, ...)
{
    char msg[60]; // fixed length limit
    va_list ap;
    va_start(ap, format);
    vsnprintf(msg, 60, format, ap);
    va_end(ap);
    decoder_output_message(msg);
}

void decoder_output_bitbufferf(bitbuffer_t const *bitbuffer, char const *restrict format, ...)
{
    char msg[60]; // fixed length limit
    va_list ap;
    va_start(ap, format);
    vsnprintf(msg, 60, format, ap);
    va_end(ap);
    decoder_output_bitbuffer(bitbuffer, msg);
}

void decoder_output_bitbuffer_arrayf(bitbuffer_t const *bitbuffer, char const *restrict format, ...)
{
    char msg[60]; // fixed length limit
    va_list ap;
    va_start(ap, format);
    vsnprintf(msg, 60, format, ap);
    va_end(ap);
    decoder_output_bitbuffer_array(bitbuffer, msg);
}

void decoder_output_bitrowf(bitrow_t const bitrow, unsigned bit_len, char const *restrict format, ...)
{
    char msg[60]; // fixed length limit
    va_list ap;
    va_start(ap, format);
    vsnprintf(msg, 60, format, ap);
    va_end(ap);
    decoder_output_bitrow(bitrow, bit_len, msg);
}

// output functions

void decoder_output_message(char const *msg)
{
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];

    local_time_str(0, time_str);
    data = data_make(
            "time", "", DATA_STRING, time_str,
            "msg", "", DATA_STRING, msg,
            NULL);
    data_acquired_handler(data);
}

void decoder_output_bitbuffer(bitbuffer_t const *bitbuffer, char const *msg)
{
    data_t *data;
    char *row_codes[BITBUF_ROWS];
    char row_bytes[BITBUF_COLS * 2 + 1];
    char time_str[LOCAL_TIME_BUFLEN];
    unsigned i;

    local_time_str(0, time_str);

    for (i = 0; i < bitbuffer->num_rows; i++) {
        row_bytes[0] = '\0';
        // print byte-wide
        for (unsigned col = 0; col < (bitbuffer->bits_per_row[i] + 7) / 8; ++col) {
            sprintf(&row_bytes[2 * col], "%02x", bitbuffer->bb[i][col]);
        }
        // remove last nibble if needed
        row_bytes[2 * (bitbuffer->bits_per_row[i] + 3) / 8] = '\0';

        // a simpler representation for csv output
        row_codes[i] = malloc(8 + BITBUF_COLS * 2 + 1); // "{nnn}..\0"
        sprintf(row_codes[i], "{%d}%s", bitbuffer->bits_per_row[i], row_bytes);
    }

    data = data_make(
            "time", "", DATA_STRING, time_str,
            "msg", "", DATA_STRING, msg,
            "num_rows", "", DATA_INT, bitbuffer->num_rows,
            "codes", "", DATA_ARRAY, data_array(bitbuffer->num_rows, DATA_STRING, row_codes),
            NULL);
    data_acquired_handler(data);

    for (i = 0; i < bitbuffer->num_rows; i++) {
        free(row_codes[i]);
    }
}

void decoder_output_bitbuffer_array(bitbuffer_t const *bitbuffer, char const *msg)
{
    data_t *data;
    data_t *row_data[BITBUF_ROWS];
    char *row_codes[BITBUF_ROWS];
    char row_bytes[BITBUF_COLS * 2 + 1];
    char time_str[LOCAL_TIME_BUFLEN];
    unsigned i;

    local_time_str(0, time_str);

    for (i = 0; i < bitbuffer->num_rows; i++) {
        row_bytes[0] = '\0';
        // print byte-wide
        for (unsigned col = 0; col < (bitbuffer->bits_per_row[i] + 7) / 8; ++col) {
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
            "time", "", DATA_STRING, time_str,
            "msg", "", DATA_STRING, msg,
            "num_rows", "", DATA_INT, bitbuffer->num_rows,
            "rows", "", DATA_ARRAY, data_array(bitbuffer->num_rows, DATA_DATA, row_data),
            "codes", "", DATA_ARRAY, data_array(bitbuffer->num_rows, DATA_STRING, row_codes),
            NULL);
    data_acquired_handler(data);

    for (i = 0; i < bitbuffer->num_rows; i++) {
        free(row_codes[i]);
    }
}

void decoder_output_bitrow(bitrow_t const bitrow, unsigned bit_len, char const *msg)
{
    data_t *data;
    char *row_code;
    char row_bytes[BITBUF_COLS * 2 + 1];
    char time_str[LOCAL_TIME_BUFLEN];
    unsigned i;

    local_time_str(0, time_str);

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
            "time", "", DATA_STRING, time_str,
            "msg", "", DATA_STRING, msg,
            "codes", "", DATA_STRING, row_code,
            NULL);
    data_acquired_handler(data);

    free(row_code);
}
