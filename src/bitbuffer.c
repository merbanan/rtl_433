/** @file
    A two-dimensional bit buffer consisting of bytes.

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "bitbuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bitbuffer_clear(bitbuffer_t *bits)
{
    bits->num_rows = 0;
    memset(bits->bits_per_row, 0, BITBUF_ROWS * 2);
    memset(bits->bb, 0, BITBUF_ROWS * BITBUF_COLS);
}

void bitbuffer_add_bit(bitbuffer_t *bits, int bit)
{
    if (bits->num_rows == 0)
        bits->num_rows++; // Add first row automatically
    uint16_t col_index = bits->bits_per_row[bits->num_rows - 1] / 8;
    uint16_t bit_index = bits->bits_per_row[bits->num_rows - 1] % 8;
    if ((col_index < BITBUF_COLS) && (bits->num_rows <= BITBUF_ROWS)) {
        bits->bb[bits->num_rows - 1][col_index] |= (bit << (7 - bit_index));
        bits->bits_per_row[bits->num_rows - 1]++;
    }
    else {
        // fprintf(stderr, "ERROR: bitbuffer:: Could not add more columns\n");    // Some decoders may add many columns...
    }
}

void bitbuffer_add_row(bitbuffer_t *bits)
{
    if (bits->num_rows == 0)
        bits->num_rows++; // Add first row automatically
    if (bits->num_rows < BITBUF_ROWS) {
        bits->num_rows++;
    }
    else {
        bits->bits_per_row[bits->num_rows - 1] = 0; // Clear last row to handle overflow somewhat gracefully
        // fprintf(stderr, "ERROR: bitbuffer:: Could not add more rows\n");    // Some decoders may add many rows...
    }
}

void bitbuffer_add_sync(bitbuffer_t *bits)
{
    if (bits->num_rows == 0)
        bits->num_rows++; // Add first row automatically
    if (bits->bits_per_row[bits->num_rows - 1]) {
        bitbuffer_add_row(bits);
    }
    bits->syncs_before_row[bits->num_rows - 1]++;
}

void bitbuffer_invert(bitbuffer_t *bits)
{
    for (unsigned row = 0; row < bits->num_rows; ++row) {
        if (bits->bits_per_row[row] > 0) {
            const unsigned last_col  = (bits->bits_per_row[row] - 1) / 8;
            const unsigned last_bits = ((bits->bits_per_row[row] - 1) % 8) + 1;
            for (unsigned col = 0; col <= last_col; ++col) {
                bits->bb[row][col] = ~bits->bb[row][col]; // Invert
            }
            bits->bb[row][last_col] ^= 0xFF >> last_bits; // Re-invert unused bits in last byte
        }
    }
}

void bitbuffer_nrzs_decode(bitbuffer_t *bits)
{
    for (unsigned row = 0; row < bits->num_rows; ++row) {
        if (bits->bits_per_row[row] > 0) {
            const unsigned last_col  = (bits->bits_per_row[row] - 1) / 8;
            const unsigned last_bits = ((bits->bits_per_row[row] - 1) % 8) + 1;

            int prev = 0;
            for (unsigned col = 0; col <= last_col; ++col) {
                int mask           = (prev << 7) | bits->bb[row][col] >> 1;
                prev               = bits->bb[row][col];
                bits->bb[row][col] = bits->bb[row][col] ^ ~mask;
            }
            bits->bb[row][last_col] &= 0xFF << (8 - last_bits); // Clear unused bits in last byte
        }
    }
}

void bitbuffer_nrzm_decode(bitbuffer_t *bits)
{
    for (unsigned row = 0; row < bits->num_rows; ++row) {
        if (bits->bits_per_row[row] > 0) {
            const unsigned last_col  = (bits->bits_per_row[row] - 1) / 8;
            const unsigned last_bits = ((bits->bits_per_row[row] - 1) % 8) + 1;

            int prev = 0;
            for (unsigned col = 0; col <= last_col; ++col) {
                int mask           = (prev << 7) | bits->bb[row][col] >> 1;
                prev               = bits->bb[row][col];
                bits->bb[row][col] = bits->bb[row][col] ^ mask;
            }
            bits->bb[row][last_col] &= 0xFF << (8 - last_bits); // Clear unused bits in last byte
        }
    }
}

void bitbuffer_extract_bytes(bitbuffer_t *bitbuffer, unsigned row,
        unsigned pos, uint8_t *out, unsigned len)
{
    uint8_t *bits = bitbuffer->bb[row];
    if (len == 0)
        return;
    if ((pos & 7) == 0) {
        memcpy(out, bits + (pos / 8), (len + 7) / 8);
    }
    else {
        unsigned shift = 8 - (pos & 7);
        unsigned bytes = (len + 7) >> 3;
        uint8_t *p = out;
        uint16_t word;
        pos = pos >> 3; // Convert to bytes

        word = bits[pos];

        while (bytes--) {
            word <<= 8;
            word |= bits[++pos];
            *(p++) = word >> shift;
        }
    }
    if (len & 7)
        out[(len - 1) / 8] &= 0xff00 >> (len & 7); // mask off bottom bits
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
    unsigned len  = bitbuffer->bits_per_row[row];
    unsigned ipos = start;
    unsigned ppos = 0; // cursor on init pattern

    while (ipos < len && ppos < pattern_bits_len) {
        if (bit(bits, ipos) == bit(pattern, ppos)) {
            ppos++;
            ipos++;
            if (ppos == pattern_bits_len)
                return ipos - pattern_bits_len;
        }
        else {
            ipos -= ppos;
            ipos++;
            ppos = 0;
        }
    }

    // Not found
    return len;
}

unsigned bitbuffer_manchester_decode(bitbuffer_t *inbuf, unsigned row, unsigned start,
        bitbuffer_t *outbuf, unsigned max)
{
    uint8_t *bits     = inbuf->bb[row];
    unsigned int len  = inbuf->bits_per_row[row];
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

unsigned bitbuffer_differential_manchester_decode(bitbuffer_t *inbuf, unsigned row, unsigned start,
        bitbuffer_t *outbuf, unsigned max)
{
    uint8_t *bits     = inbuf->bb[row];
    unsigned int len  = inbuf->bits_per_row[row];
    unsigned int ipos = start;
    uint8_t bit1, bit2 = 0, bit3;

    if (max && len > start + (max * 2))
        len = start + (max * 2);

    // the first long pulse will determine the clock
    // if needed skip one short pulse to get in synch
    while (ipos < len) {
        bit1 = bit(bits, ipos++);
        bit2 = bit(bits, ipos++);
        bit3 = bit(bits, ipos);

        if (bit1 != bit2) {
            if (bit2 != bit3) {
                bitbuffer_add_bit(outbuf, 0);
            }
            else {
                bit2 = bit1;
                ipos -= 1;
                break;
            }
        }
        else {
            bit2 = 1 - bit1;
            ipos -= 2;
            break;
        }
    }

    while (ipos < len) {
        bit1 = bit(bits, ipos++);
        if (bit1 == bit2)
            break; // clock missing, abort
        bit2 = bit(bits, ipos++);

        if (bit1 == bit2)
            bitbuffer_add_bit(outbuf, 1);
        else
            bitbuffer_add_bit(outbuf, 0);
    }

    return ipos;
}

static void print_bitrow(bitrow_t const bitrow, unsigned bit_len, unsigned highest_indent, int always_binary)
{
    unsigned row_len = 0;

    fprintf(stderr, "{%2d} ", bit_len);
    for (unsigned col = 0; col < (bit_len + 7) / 8; ++col) {
        row_len += fprintf(stderr, "%02x ", bitrow[col]);
    }
    // Print binary values also?
    if (always_binary || bit_len <= BITBUF_MAX_PRINT_BITS) {
        fprintf(stderr, "%-*s: ", highest_indent > row_len ? highest_indent - row_len : 0, "");
        for (unsigned bit = 0; bit < bit_len; ++bit) {
            if (bitrow[bit / 8] & (0x80 >> (bit % 8))) {
                fprintf(stderr, "1");
            }
            else {
                fprintf(stderr, "0");
            }
            if ((bit % 8) == 7) // Add byte separators
                fprintf(stderr, " ");
        }
    }
    fprintf(stderr, "\n");
}

static void print_bitbuffer(const bitbuffer_t *bits, int always_binary)
{
    unsigned highest_indent, indent_this_col, indent_this_row;
    unsigned col, row;

    /* Figure out the longest row of bit to get the highest_indent
     */
    highest_indent = sizeof("[dd] {dd} ") - 1;
    for (row = indent_this_row = 0; row < bits->num_rows; ++row) {
        for (col = indent_this_col = 0; col < (unsigned)(bits->bits_per_row[row] + 7) / 8; ++col) {
            indent_this_col += 2 + 1;
        }
        indent_this_row = indent_this_col;
        if (indent_this_row > highest_indent)
            highest_indent = indent_this_row;
    }

    fprintf(stderr, "bitbuffer:: Number of rows: %d \n", bits->num_rows);
    for (row = 0; row < bits->num_rows; ++row) {
        fprintf(stderr, "[%02d] ", row);
        print_bitrow(bits->bb[row], bits->bits_per_row[row], highest_indent, always_binary);
    }
    if (bits->num_rows >= BITBUF_ROWS) {
        fprintf(stderr, "... Maximum number of rows reached. Message is likely truncated.\n");
    }
}

void bitbuffer_print(const bitbuffer_t *bits)
{
    print_bitbuffer(bits, 0);
}

void bitbuffer_debug(const bitbuffer_t *bits)
{
    print_bitbuffer(bits, 1);
}

void bitrow_print(bitrow_t const bitrow, unsigned bit_len)
{
    print_bitrow(bitrow, bit_len, 0, 0);
}

void bitrow_debug(bitrow_t const bitrow, unsigned bit_len)
{
    print_bitrow(bitrow, bit_len, 0, 1);
}

void bitbuffer_parse(bitbuffer_t *bits, const char *code)
{
    const char *c;
    int data  = 0;
    int width = -1;

    bitbuffer_clear(bits);

    for (c = code; *c; ++c) {

        if (*c == ' ') {
            continue;
        }
        else if (*c == '0' && (*(c + 1) == 'x' || *(c + 1) == 'X')) {
            ++c;
            continue;
        }
        else if (*c == '{') {
            if (bits->num_rows == 0) {
                bits->num_rows++;
            }
            else {
                bitbuffer_add_row(bits);
            }
            if (width >= 0) {
                bits->bits_per_row[bits->num_rows - 2] = width;
            }

            width = strtol(c + 1, (char **)&c, 0);
            continue;
        }
        else if (*c == '/') {
            bitbuffer_add_row(bits);
            if (width >= 0) {
                bits->bits_per_row[bits->num_rows - 2] = width;
                width = -1;
            }
            continue;
        }
        else if (*c >= '0' && *c <= '9') {
            data = *c - '0';
        }
        else if (*c >= 'A' && *c <= 'F') {
            data = *c - 'A' + 10;
        }
        else if (*c >= 'a' && *c <= 'f') {
            data = *c - 'a' + 10;
        }
        bitbuffer_add_bit(bits, data >> 3 & 0x01);
        bitbuffer_add_bit(bits, data >> 2 & 0x01);
        bitbuffer_add_bit(bits, data >> 1 & 0x01);
        bitbuffer_add_bit(bits, data >> 0 & 0x01);
    }
    if (width >= 0) {
        if (bits->num_rows == 0) {
            bits->num_rows++;
        }
        bits->bits_per_row[bits->num_rows - 1] = width;
    }
}

int compare_rows(bitbuffer_t *bits, unsigned row_a, unsigned row_b)
{
    return (bits->bits_per_row[row_a] == bits->bits_per_row[row_b]
            && !memcmp(bits->bb[row_a], bits->bb[row_b],
                    (bits->bits_per_row[row_a] + 7) / 8));
}

unsigned count_repeats(bitbuffer_t *bits, unsigned row)
{
    unsigned cnt = 0;
    for (int i = 0; i < bits->num_rows; ++i) {
        if (compare_rows(bits, row, i)) {
            ++cnt;
        }
    }
    return cnt;
}

int bitbuffer_find_repeated_row(bitbuffer_t *bits, unsigned min_repeats, unsigned min_bits)
{
    for (int i = 0; i < bits->num_rows; ++i) {
        if (bits->bits_per_row[i] >= min_bits &&
                count_repeats(bits, i) >= min_repeats) {
            return i;
        }
    }
    return -1;
}

// Unit testing
#ifdef _TEST
#include <assert.h>
int main(int argc, char **argv)
{
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
    for (int i = 0; i < BITBUF_COLS * 8; ++i) {
        bitbuffer_add_bit(&bits, i % 2);
    }
    bitbuffer_print(&bits);

    fprintf(stderr, "TEST: bitbuffer:: Add row and fill 1 column too many\n");
    bitbuffer_add_row(&bits);
    for (int i = 0; i <= BITBUF_COLS * 8; ++i) {
        bitbuffer_add_bit(&bits, i % 2);
    }
    bitbuffer_print(&bits);

    fprintf(stderr, "TEST: bitbuffer:: invert\n");
    bitbuffer_invert(&bits);
    bitbuffer_print(&bits);

    fprintf(stderr, "TEST: bitbuffer:: nrzs_decode\n");
    bits.num_rows = 1;
    bits.bb[0][0] = 0x74;
    bits.bb[0][1] = 0x60;
    bits.bits_per_row[0] = 12;
    bitbuffer_nrzs_decode(&bits);
    bitbuffer_print(&bits);
    assert(bits.bb[0][0] == 0xB1);
    assert(bits.bb[0][1] == 0xA0);

    fprintf(stderr, "TEST: bitbuffer:: Clear\n");
    bitbuffer_clear(&bits);
    bitbuffer_print(&bits);

    fprintf(stderr, "TEST: bitbuffer:: Add 1 row too many\n");
    for (int i = 0; i <= BITBUF_ROWS; ++i) {
        bitbuffer_add_row(&bits);
    }
    bitbuffer_add_bit(&bits, 1);
    bitbuffer_print(&bits);

    return 0;
}
#endif /* _TEST */
