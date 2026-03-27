/** @file
    Arad/Master Meter Dialog3G water utility meter.

    Copyright (C) 2022 avicarmeli

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARAD_MM_MAX_SERIAL_FILTER 64

typedef struct {
    uint32_t ser24; // 24-bit serial
    int suffix;     // 0..255 if valid, else -1 (ignore suffix)
} arad_mm_serial_filter_t;

typedef enum {
    ARAD_UNIT_M3 = 0,
    ARAD_UNIT_LITERS,
    ARAD_UNIT_CF,
    ARAD_UNIT_USG,
} arad_unit_t;

typedef struct {
    // serial filter (MANDATORY)
    unsigned count;
    arad_mm_serial_filter_t items[ARAD_MM_MAX_SERIAL_FILTER];

    // user overrides
    int user_gear_set;
    double user_gear;

    int user_units_set; // overrides NATIVE unit
    arad_unit_t user_units;

    int user_convert_set; // output conversion
    arad_unit_t user_convert;

    int warned_no_serial;

    int filter_mode;
} arad_mm_ctx_t;

static char *arad_trim_inplace(char *s)
{
    if (!s)
        return s;
    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return s;

    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) {
        *e = '\0';
        --e;
    }
    return s;
}

static int arad_ieq(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static char *arad_strtok(char *str, const char *delim, char **saveptr)
{
#ifdef _MSC_VER
    return strtok_s(str, delim, saveptr);
#else
    return strtok_r(str, delim, saveptr);
#endif
}

static int arad_parse_u32_auto(const char *s, uint32_t *out)
{
    if (!s || !out)
        return 0;
    while (*s && isspace((unsigned char)*s))
        s++;

    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    else {
        for (const char *p = s; *p; ++p) {
            if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                base = 16;
                break;
            }
        }
    }

    char *endp      = NULL;
    unsigned long v = strtoul(s, &endp, base);
    if (endp == s)
        return 0;
    while (*endp && isspace((unsigned char)*endp))
        endp++;
    if (*endp != '\0')
        return 0;

    *out = (uint32_t)v;
    return 1;
}

static void arad_ctx_add_filter(arad_mm_ctx_t *ctx, uint32_t ser24, int suffix_or_minus1)
{
    if (!ctx)
        return;
    if (ctx->count >= ARAD_MM_MAX_SERIAL_FILTER)
        return;

    ctx->items[ctx->count].ser24  = (ser24 & 0x00ffffffu);
    ctx->items[ctx->count].suffix = suffix_or_minus1;
    ctx->count++;
}

static int arad_sum_decimal_digits(const char *s, unsigned *digits_out)
{
    if (!s)
        return -1;
    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return -1;

    unsigned digits = 0;
    int sum         = 0;
    for (const char *p = s; *p; ++p) {
        if (isspace((unsigned char)*p))
            continue;
        if (*p < '0' || *p > '9')
            return -1;
        sum += (int)(*p - '0');
        digits++;
    }

    if (digits_out)
        *digits_out = digits;
    return sum;
}

static int arad_token_is_special(const char *tok)
{
    unsigned digits = 0;
    int sum         = arad_sum_decimal_digits(tok, &digits);
    if (sum < 0)
        return 0;

    if (digits != 8)
        return 0;

    int expected = 0;
    for (unsigned i = 0; i < digits; ++i)
        expected += 9;

    return sum == expected;
}

static void arad_parse_serial_list(arad_mm_ctx_t *ctx, char *list)
{
    if (!ctx || !list)
        return;

    // split by comma/semicolon/whitespace into NUL-separated tokens
    for (char *p = list; *p; ++p) {
        if (*p == ',' || *p == ';' || isspace((unsigned char)*p))
            *p = '\0';
    }

    char *p   = list;
    char *end = list + strlen(list) + 1;
    while (p < end) {
        if (!*p) {
            ++p;
            continue;
        }

        char *tok = arad_trim_inplace(p);
        p += strlen(p) + 1;
        if (!tok || !*tok)
            continue;

        if (arad_token_is_special(tok)) {
            ctx->filter_mode = 0;
            continue;
        }

        // Accept "SERIAL-SUFFIX"
        char *dash = strchr(tok, '-');
        if (dash) {
            *dash       = '\0';
            char *ser_s = arad_trim_inplace(tok);
            char *suf_s = arad_trim_inplace(dash + 1);

            uint32_t ser_v = 0, suf_v = 0;
            if (arad_parse_u32_auto(ser_s, &ser_v) && arad_parse_u32_auto(suf_s, &suf_v)) {
                if (suf_v <= 0xffu) {
                    arad_ctx_add_filter(ctx, ser_v, (int)suf_v);
                }
            }
            continue;
        }

        // Just serial
        uint32_t ser_v = 0;
        if (arad_parse_u32_auto(tok, &ser_v)) {
            arad_ctx_add_filter(ctx, ser_v, -1);
        }
    }
}

static int arad_serial_matches_mandatory(const arad_mm_ctx_t *ctx, uint32_t ser24, uint8_t suffix)
{
    if (!ctx)
        return 0;
    if (ctx->count == 0)
        return 0; // mandatory

    ser24 &= 0x00ffffffu;
    for (unsigned i = 0; i < ctx->count; ++i) {
        if (ctx->items[i].ser24 != ser24)
            continue;
        if (ctx->items[i].suffix < 0)
            return 1;
        if ((uint8_t)ctx->items[i].suffix == suffix)
            return 1;
    }
    return 0;
}

static int arad_parse_gear(const char *s, double *out)
{
    if (!s || !out)
        return 0;
    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return 0;

    if (strcmp(s, "0.01") == 0) {
        *out = 0.01;
        return 1;
    }
    if (strcmp(s, "0.1") == 0) {
        *out = 0.1;
        return 1;
    }
    if (strcmp(s, "1") == 0) {
        *out = 1.0;
        return 1;
    }
    if (strcmp(s, "10") == 0) {
        *out = 10.0;
        return 1;
    }
    if (strcmp(s, "100") == 0) {
        *out = 100.0;
        return 1;
    }

    if (strcmp(s, "1.0") == 0) {
        *out = 1.0;
        return 1;
    }
    if (strcmp(s, "10.0") == 0) {
        *out = 10.0;
        return 1;
    }
    if (strcmp(s, "100.0") == 0) {
        *out = 100.0;
        return 1;
    }

    return 0;
}

static int arad_parse_unit(const char *s, arad_unit_t *out)
{
    if (!s || !out)
        return 0;
    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return 0;

    if (arad_ieq(s, "m3")) {
        *out = ARAD_UNIT_M3;
        return 1;
    }
    if (arad_ieq(s, "liters")) {
        *out = ARAD_UNIT_LITERS;
        return 1;
    }
    if (arad_ieq(s, "liter")) {
        *out = ARAD_UNIT_LITERS;
        return 1;
    }
    if (arad_ieq(s, "l")) {
        *out = ARAD_UNIT_LITERS;
        return 1;
    }
    if (arad_ieq(s, "cf")) {
        *out = ARAD_UNIT_CF;
        return 1;
    }
    if (arad_ieq(s, "usg")) {
        *out = ARAD_UNIT_USG;
        return 1;
    }
    if (arad_ieq(s, "gallon")) {
        *out = ARAD_UNIT_USG;
        return 1;
    }
    if (arad_ieq(s, "gallons")) {
        *out = ARAD_UNIT_USG;
        return 1;
    }

    return 0;
}

static const char *arad_unit_str(arad_unit_t u)
{
    switch (u) {
    case ARAD_UNIT_M3: return "m3";
    case ARAD_UNIT_LITERS: return "Liters";
    case ARAD_UNIT_CF: return "CF";
    case ARAD_UNIT_USG: return "USG";
    default: return "m3";
    }
}

static double arad_units_to_m3(arad_unit_t u, double v_in_units)
{
    switch (u) {
    case ARAD_UNIT_M3: return v_in_units;
    case ARAD_UNIT_LITERS: return v_in_units / 1000.0;
    case ARAD_UNIT_CF: return v_in_units * 0.028316846592;
    case ARAD_UNIT_USG: return v_in_units * 0.003785411784;
    default: return v_in_units;
    }
}

static double arad_m3_to_units(arad_unit_t u, double v_m3)
{
    switch (u) {
    case ARAD_UNIT_M3: return v_m3;
    case ARAD_UNIT_LITERS: return v_m3 * 1000.0;
    case ARAD_UNIT_CF: return v_m3 / 0.028316846592;
    case ARAD_UNIT_USG: return v_m3 / 0.003785411784;
    default: return v_m3;
    }
}

static void arad_auto_gear_units(uint8_t after0, uint8_t after1, double *gear, arad_unit_t *unit)
{
    // Defaults
    double g      = 0.1;
    arad_unit_t u = ARAD_UNIT_M3;

    // Rules
    if (after0 == 0x73 && after1 == 0x00) {
        g = 0.1;
        u = ARAD_UNIT_M3;
    }
    else if (after0 == 0x00 && (after1 == 0x00 || after1 == 0x40)) {
        g = 0.01;
        u = ARAD_UNIT_M3;
    }
    else if (after0 == 0x27 && after1 == 0x00) {
        g = 0.1;
        u = ARAD_UNIT_USG;
    }

    if (gear)
        *gear = g;
    if (unit)
        *unit = u;
}

/*  Build a contiguous nibble-range pattern from the full 48-bit preamble.
        start_nibble/end_nibble are in [0..12], where 0 is the HIGH nibble of 0x96.
    end_nibble is exclusive. Required: 0 <= start < end <= 12.

        Output:
        out_bytes: pattern packed MSB-first
        out_bits:  number of bits to search (multiple of 4)
        Returns 1 on success, 0 on invalid input.
 */
static int arad_build_preamble_nibble_pattern(
        uint8_t out_bytes[6],
        unsigned *out_bits,
        unsigned start_nibble,
        unsigned end_nibble)
{
    static const uint8_t full[6] = {0x96, 0xf5, 0x13, 0x85, 0x37, 0xb4};

    if (!out_bytes || !out_bits)
        return 0;
    if (start_nibble >= end_nibble || end_nibble > 12)
        return 0;

    // Expand full preamble into 12 nibbles: [0]=0x9, [1]=0x6, [2]=0xF, [3]=0x5, ...
    uint8_t n[12];
    for (unsigned i = 0; i < 6; ++i) {
        n[2 * i + 0] = (uint8_t)((full[i] >> 4) & 0x0F);
        n[2 * i + 1] = (uint8_t)(full[i] & 0x0F);
    }

    const unsigned nib_count = end_nibble - start_nibble; // >= 1
    const unsigned bit_count = nib_count * 4;

    // Pack nibbles MSB-first into bytes (bitbuffer_search expects MSB-first patterns)
    memset(out_bytes, 0, 6);
    unsigned bitpos = 0;
    for (unsigned i = 0; i < nib_count; ++i) {
        uint8_t nib = n[start_nibble + i] & 0x0F;
        for (unsigned b = 0; b < 4; ++b) {
            // take nib MSB -> LSB
            unsigned bit = (nib >> (3 - b)) & 1u;

            unsigned byte_index  = bitpos / 8;
            unsigned bit_in_byte = 7u - (bitpos % 8); // MSB-first
            if (bit)
                out_bytes[byte_index] |= (uint8_t)(1u << bit_in_byte);

            bitpos++;
        }
    }

    *out_bits = bit_count;
    return 1;
}

/* 	Build a string containing the INVERTED preamble nibbles
        NOT covered by [start_nibble, end_nibble).
        The unmatched parts BEFORE and AFTER the matched window
        are separated by "_..._".

        Inversion is done per nibble: out = 0xF ^ in.
        Output format: hex nibbles (uppercase), no separators.
 */
static void arad_format_unmatched_preamble(
        char *out,
        size_t out_sz,
        unsigned start_nibble,
        unsigned end_nibble)
{
    static const uint8_t full[6] = {0x96, 0xf5, 0x13, 0x85, 0x37, 0xb4};

    if (!out || out_sz == 0)
        return;
    out[0] = '\0';

    /* Expand full preamble into 12 nibbles */
    uint8_t n[12];
    for (unsigned i = 0; i < 6; ++i) {
        n[2 * i + 0] = (uint8_t)((full[i] >> 4) & 0x0F);
        n[2 * i + 1] = (uint8_t)(full[i] & 0x0F);
    }

    size_t w         = 0;
    int wrote_before = 0;

    /* BEFORE matched window */
    for (unsigned i = 0; i < start_nibble; ++i) {
        if (w + 1 >= out_sz)
            break;
        uint8_t inv  = (uint8_t)(n[i] ^ 0x0F);
        out[w++]     = (char)((inv < 10) ? ('0' + inv) : ('A' + (inv - 10)));
        out[w]       = '\0';
        wrote_before = 1;
    }

    /* Separator between BEFORE and AFTER (only if there is AFTER) */
    if (end_nibble < 12) {
        const char sep[] = "_..._";
        size_t sep_len   = sizeof(sep) - 1;

        if (w + sep_len < out_sz) {
            memcpy(&out[w], sep, sep_len);
            w += sep_len;
            out[w] = '\0';
        }
    }

    /* AFTER matched window */
    for (unsigned i = end_nibble; i < 12; ++i) {
        if (w + 1 >= out_sz)
            break;
        uint8_t inv = (uint8_t)(n[i] ^ 0x0F);
        out[w++]    = (char)((inv < 10) ? ('0' + inv) : ('A' + (inv - 10)));
        out[w]      = '\0';
    }
}

/* ---------- decoder ---------- */

static r_device *arad_ms_meter_create(char *args);

/**
Overview
--------

Decoder for Arad / Master Meter Dialog3G water utility meter transmissions.

FCC-Id: TKCET-733

References:
- https://45851052.fs1.hubspotusercontent-na1.net/hubfs/45851052/documents/files/Interpreter-II-Register_v0710.20F.pdf
- https://www.arad.co.il/wp-content/uploads/Dialog-3G-register-information-sheet_Eng-002.pdf

Messages are transmitted approximately once every 30 seconds.

Observed message structure
--------------------------

A typical frame (after preamble) appears as:

    00000000FFFFFFFFFFFFFFSSSSSSSSXXCCCCCCXXXF?????????XFF

Field description (based on reverse engineering and observations):

- 00000000
    Preamble used for frame synchronization.

- FFFFFFFFFFFFFF
    Bytes that appear constant in time and are usually identical for
    meters located in the same neighborhood.

    Observed payload example:
        3e690aec7ac84b

    The exact meaning is unknown. It may be related to the meter register
    configuration or gearing parameters.

- SSSSSSSS
    Meter serial field (4 bytes).

    The first 3 bytes represent the numeric serial number (little-endian).

    Example:
        fa1c9073

        fa1c90  -> 09444602 (decimal serial number)
        73      -> suffix byte

    The last byte sometimes corresponds to a letter printed after the
    serial on the physical meter, but not always. In many cases it contains
    values that do not appear on the meter label.

    Empirically, this suffix byte together with the following byte appears
    to encode meter configuration parameters such as the measurement unit
    and gear (resolution).

- XX
    Unknown field (2 bytes). Its role is not yet understood.

- CCCCCC
    Counter value (3 bytes, little-endian).

    Example:
        a80600 -> 1704

    This value represents the raw meter counter before applying the gear
    multiplier.

- XXX
    Unknown field (3 bytes). Function currently unknown.

- F
    Observed constant byte in many frames.

    Typical observed payload:
        0x05

    Appears stable for meters in the same installation but the exact
    meaning is not yet confirmed.

- ?????????
    Likely CRC or checksum field. The exact algorithm is currently unknown.

- X
    A single byte that is typically observed as either:

        0x08  or  0x00

    The meaning is not yet known.

- FF
    Final byte of the frame.

    Earlier observations suggested this byte was constant (often 0xF8),
    however newer captures show that this byte may change over time even
    for the same meter. Its purpose is currently unknown and it should not
    be assumed constant.

Notes
-----

Several fields previously assumed constant have been observed to vary in
newer captures. This documentation reflects current understanding based on
empirical analysis and may be updated as additional frames are decoded.

BitBench notation
-----------------

Use PREAMBLE_ALIGN with value:

    c196f51385

This is the inverted form of:

    3e690aec7a

Format string:

    UID:16h SERIAL:<24d 8h 8h COUNTER:<32d 8h8h 8h8h 8h8h SUFFIX:hh

Protocol options
----------------

This decoder accepts arguments through:

    rtl_433 -R ID:ARGS

Mandatory:
- serial=LIST / serials=LIST
    At least one serial must be provided, otherwise the decoder is silent
    and prints a warning once.

    The filter accepts one or more meter serials (24-bit little-endian),
    in decimal or hex notation.

    A suffix byte may optionally be locked too by using:

        SERIAL-SUFFIX

    Examples:
        -R ID:serial=9444602
        -R ID:serials=9444602;1234567;0xfa1c90
        -R ID:serials=09444602-73;01234567-53

    NOTE:
    If other options are combined with serials, commas separate key=value
    pairs. In that case prefer semicolons inside serials:

        -R ID:serials=9444602;1234567,gear=0.1,units=m3

Optional:
- gear=VALUE
    Flow multiplier / resolution.
    Allowed values: 0.01, 0.1, 1, 10, 100
    Default: 0.1

- units=VALUE
    Overrides the native unit interpretation of the meter.
    Allowed values (case-insensitive): m3, Liters, CF, USG

- convert=VALUE
    Converts the decoded numeric volume to the requested output unit.
    Allowed values (case-insensitive): m3, Liters, CF, USG

Outputs
-------

- model               : decoder model name
- id                  : SERIAL-SUFFIX
                        serial as 8-digit decimal + '-' + suffix as hex
- volume              : decoded volume in selected output units
- unit                : output unit string (m3 / Liters / CF / USG)
- volume_m3           : volume always provided in cubic meters
- gear                : effective native gear used for decoding
- native_unit         : effective native unit used for decoding
- unmatched_preamble  : inverted preamble nibbles outside the matched
                        nibble window, formatted as:

                            BEFORE_..._AFTER

                        where BEFORE is the unmatched part before the
                        matched preamble window and AFTER is the unmatched
                        part after it

Preamble detection
------------------

Full preamble (already in the expected polarity for search):

    96 f5 13 85 37 b4

The decoder may search only a configurable nibble window inside this full
preamble. The unmatched nibbles are not validated and are reported in the
output as unmatched_preamble after nibble-wise inversion.

Important:
- The payload extraction offset is always derived from the start of the
  full 48-bit preamble, even if only a sub-range of nibbles is matched.
- This ensures serial / suffix / counter extraction stays aligned when the
  preamble match window is changed.

Buffer polarity
---------------

This decoder keeps the original polarity behavior:

    bitbuffer_invert(bitbuffer);

The preamble is located first, and the shared bitbuffer is inverted only
afterwards, before payload extraction.

As a result, verbose (-V) output shows the buffer in the polarity expected
by this decoder.

Decoding behavior
-----------------

- serial/serials is mandatory
- the decoder is silent when no serial filter is provided
- a warning is printed once if serial/serials is missing
- automatic native gear / unit detection is based on bytes after the serial:

    b[3] == 0x73 && b[4] == 0x00
        -> gear = 0.1, native_unit = m3

    b[3] == 0x00 && (b[4] == 0x00 || b[4] == 0x40)
        -> gear = 0.01, native_unit = m3

    b[3] == 0x27 && b[4] == 0x00
        -> gear = 0.1, native_unit = USG

    default
        -> gear = 0.1, native_unit = m3

Override order:

    auto -> gear override -> units override -> convert

Meaning:
- gear=...    overrides the detected native gear
- units=...   overrides the detected native unit
- convert=... converts the numeric output volume to the requested unit

Notes:
- units= changes the native interpretation
- convert= changes the displayed numeric output
- convert overrides units in output selection

Implementation notes
--------------------

- MSVC / Windows compatible tokenizer
- no strtok_r
- no DATA_FORMAT varargs usage
- output uses separate numeric and unit fields

*/

static int arad_mm_dialog3g_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    /* 	---- Preamble matching window (nibbles) ----
                Full preamble (inverted polarity, do NOT change):
                96 f5 13 85 37 b4

                Configure which nibbles are matched:
                nibble index 0..11, where 0 is HIGH nibble of 0x96, 11 is LOW nibble of 0xB4.
                start_nibble inclusive, end_nibble exclusive.

                Example:
                        start_nibble=0, end_nibble=12  -> match full preamble (48 bits)
                        start_nibble=2, end_nibble=10  -> match middle 8 nibbles (32 bits)
     */
    /* ---- Preamble matching window (nibbles) ---- */
    const unsigned start_nibble = 1; /* inclusive, 0..11 */
    const unsigned end_nibble   = 8; /* exclusive, 1..12 */

    uint8_t preamble_pat[6];
    unsigned preamble_bits = 0;

    if (!arad_build_preamble_nibble_pattern(preamble_pat, &preamble_bits, start_nibble, end_nibble))
        return DECODE_ABORT_EARLY;

    int row = bitbuffer_find_repeated_row(bitbuffer, 1, 168);
    if (row < 0)
        return DECODE_ABORT_EARLY;

    int match_pos_i = (int)bitbuffer_search(bitbuffer, row, 0, preamble_pat, preamble_bits);
    if (match_pos_i < 0)
        return DECODE_ABORT_EARLY;

    /* Build unmatched_preamble (already inverted + "_..._" separator in your current function) */
    char unmatched_preamble[32];
    arad_format_unmatched_preamble(unmatched_preamble, sizeof(unmatched_preamble), start_nibble, end_nibble);

    /*
                Critical fix:
                Compute the start of the FULL 48-bit preamble from the window match position,
                then start payload AFTER the full preamble (always +48), regardless of window.
     */
    int full_preamble_start_i = match_pos_i - (int)(start_nibble * 4);
    if (full_preamble_start_i < 0)
        return DECODE_ABORT_EARLY;

    unsigned payload_start = (unsigned)full_preamble_start_i + 48;

    /* Optional: ensure we have enough bits for payload */
    if (payload_start + 120 > bitbuffer->bits_per_row[row])
        return DECODE_ABORT_LENGTH;

    /* Keep original behavior: invert AFTER locating preamble */
    bitbuffer_invert(bitbuffer);

    /* Extract payload from the FIXED position */
    uint8_t b[15];
    bitbuffer_extract_bytes(bitbuffer, row, payload_start, b, 120);

    uint32_t serno    = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
    uint8_t after0    = b[3]; // byte after serial
    uint8_t after1    = b[4]; // next byte
    uint32_t wreadraw = (uint32_t)b[5] | ((uint32_t)b[6] << 8) | ((uint32_t)b[7] << 16);

    arad_mm_ctx_t *ctx = (arad_mm_ctx_t *)decoder_user_data(decoder);
    if (!ctx)
        return DECODE_ABORT_EARLY;

    if (ctx->filter_mode) {
        if (ctx->count == 0) {
            if (!ctx->warned_no_serial) {
                ctx->warned_no_serial = 1;
                fprintf(stderr,
                        "AradMsMeter-Dialog3G: serials mandatory. Example: -R 260:v:serials=13751342\n");
            }
            return DECODE_ABORT_EARLY;
        }

        // Match by serial + optional suffix (suffix matches after0)
        if (!arad_serial_matches_mandatory(ctx, serno, after0))
            return DECODE_ABORT_EARLY;
    }

    // AUTO native gear/unit
    double native_gear      = 0.1;
    arad_unit_t native_unit = ARAD_UNIT_M3;
    arad_auto_gear_units(after0, after1, &native_gear, &native_unit);

    // user overrides: gear overrides auto; units overrides NATIVE unit (as you requested)
    if (ctx->user_gear_set) {
        native_gear = ctx->user_gear;
    }
    if (ctx->user_units_set) {
        native_unit = ctx->user_units;
    }

    // Native volume in native_unit
    double volume_native = (double)wreadraw * native_gear;

    // True m3 derived from native_unit
    double volume_m3 = arad_units_to_m3(native_unit, volume_native);

    // Output selection
    double out_volume    = volume_native;
    arad_unit_t out_unit = native_unit;

    if (ctx->user_convert_set) {
        out_unit   = ctx->user_convert;
        out_volume = arad_m3_to_units(out_unit, volume_m3);
    }

    char id_out[16];
    snprintf(id_out, sizeof(id_out), "%08u-%02x", (unsigned)serno, (unsigned)after0);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",              DATA_STRING, "AradMsMeter-Dialog3G",
            "id",           "Serial No",     DATA_STRING, id_out,
            "unmtchd prambl","Unmatched Preamble Nibbles", DATA_STRING, unmatched_preamble,
            "volume",       "Volume",        DATA_DOUBLE, out_volume,
            "unit",         "Unit",          DATA_STRING, arad_unit_str(out_unit),
            "volume_m3",    "Volume (m3)",   DATA_DOUBLE, volume_m3,
            "gear",         "Gear",          DATA_DOUBLE, native_gear,
            "native_unit",  "Native Unit",   DATA_STRING, arad_unit_str(native_unit),
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "unmtchd prambl",
        "volume",
        "unit",
        "volume_m3",
        "gear",
        "native_unit",
        NULL,
};

r_device const arad_ms_meter = {
        .name        = "Arad/Master Meter Dialog3G water utility meter",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 8.4,
        .long_width  = 8.4,
        .reset_limit = 30,
        .decode_fn   = &arad_mm_dialog3g_decode,
        .create_fn   = &arad_ms_meter_create,
        .disabled    = 1,
        .fields      = output_fields,
};

static r_device *arad_ms_meter_create(char *args)
{
    r_device *dev = decoder_create(&arad_ms_meter, sizeof(arad_mm_ctx_t));
    if (!dev)
        return NULL;

    arad_mm_ctx_t *ctx = (arad_mm_ctx_t *)decoder_user_data(dev);

    ctx->count = 0;

    ctx->user_gear_set = 0;
    ctx->user_gear     = 0.1;

    ctx->user_units_set = 0;
    ctx->user_units     = ARAD_UNIT_M3;

    ctx->user_convert_set = 0;
    ctx->user_convert     = ARAD_UNIT_M3;

    ctx->warned_no_serial = 0;
    ctx->filter_mode      = 1;

    if (!args)
        return dev;
    args = arad_trim_inplace(args);
    if (!*args)
        return dev;

    // Parse key=value tokens separated by ':' or ',' (rtl_433 uses ':' between decoder args)
    char *work = strdup(args);
    if (!work)
        return dev;

    char *saveptr = NULL;
    char *tok     = arad_strtok(work, ",:", &saveptr);

    int collecting_serials = 0;
    char serial_buf[512];
    serial_buf[0] = '\0';

    while (tok) {
        char *t = arad_trim_inplace(tok);
        if (!t || !*t) {
            tok = arad_strtok(NULL, ",:", &saveptr);
            continue;
        }

        // allow lone flags like "v" (ignore), and serial list continuation
        if (!strchr(t, '=')) {
            if (collecting_serials) {
                if (serial_buf[0])
                    strncat(serial_buf, ",", sizeof(serial_buf) - strlen(serial_buf) - 1);
                strncat(serial_buf, t, sizeof(serial_buf) - strlen(serial_buf) - 1);
            }
            tok = arad_strtok(NULL, ",:", &saveptr);
            continue;
        }

        // If we were collecting serials and hit a new key=value, flush serials first
        if (collecting_serials) {
            if (serial_buf[0]) {
                arad_parse_serial_list(ctx, serial_buf);
                serial_buf[0] = '\0';
            }
            collecting_serials = 0;
        }

        char *eq = strchr(t, '=');
        if (!eq) {
            tok = arad_strtok(NULL, ",:", &saveptr);
            continue;
        }

        *eq       = '\0';
        char *key = arad_trim_inplace(t);
        char *val = arad_trim_inplace(eq + 1);
        if (!val)
            val = (char *)"";

        if (arad_ieq(key, "serial") || arad_ieq(key, "serials")) {
            collecting_serials = 1;
            serial_buf[0]      = '\0';
            if (*val) {
                snprintf(serial_buf, sizeof(serial_buf), "%s", val);
            }
        }
        else if (arad_ieq(key, "gear")) {
            double g = 0.0;
            if (arad_parse_gear(val, &g)) {
                ctx->user_gear_set = 1;
                ctx->user_gear     = g;
            }
        }
        else if (arad_ieq(key, "units")) {
            arad_unit_t u;
            if (arad_parse_unit(val, &u)) {
                ctx->user_units_set = 1;
                ctx->user_units     = u;
            }
        }
        else if (arad_ieq(key, "convert")) {
            arad_unit_t u;
            if (arad_parse_unit(val, &u)) {
                ctx->user_convert_set = 1;
                ctx->user_convert     = u;
            }
        }

        tok = arad_strtok(NULL, ",:", &saveptr);
    }

    if (collecting_serials && serial_buf[0]) {
        arad_parse_serial_list(ctx, serial_buf);
    }

    free(work);
    return dev;
}