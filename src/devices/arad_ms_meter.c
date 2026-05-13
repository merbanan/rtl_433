/** @file
    Arad/Master Meter Dialog3G water utility meter.

    Copyright (C) 2022 avicarmeli, ProfBoc75

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

#define ARAD_MM_MAX_SERIALS 64

typedef struct {
    uint32_t ser24;
    int suffix;
} arad_mm_serial_t;

typedef enum {
    ARAD_UNIT_M3 = 0,
    ARAD_UNIT_L,
    ARAD_UNIT_CF,
    ARAD_UNIT_USG,
} arad_unit_t;

typedef struct {
    unsigned count;
    arad_mm_serial_t serials[ARAD_MM_MAX_SERIALS];
    int user_gear_set;
    float user_gear;
    int user_units_set;
    arad_unit_t user_units;
} arad_mm_ctx_t;

static char *arad_trim(char *s)
{
    char *e;

    if (!s)
        return s;

    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return s;

    e = s + strlen(s) - 1;
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

static int arad_parse_u32(const char *s, uint32_t *out)
{
    char *endp = NULL;
    unsigned long v;

    if (!s || !out)
        return 0;

    v = strtoul(s, &endp, 0);
    if (endp == s)
        return 0;
    while (*endp && isspace((unsigned char)*endp))
        endp++;
    if (*endp)
        return 0;

    *out = (uint32_t)v;
    return 1;
}

static void arad_add_serial(arad_mm_ctx_t *ctx, uint32_t ser24, int suffix)
{
    if (!ctx || ctx->count >= ARAD_MM_MAX_SERIALS)
        return;

    ctx->serials[ctx->count].ser24  = ser24 & 0x00ffffffu;
    ctx->serials[ctx->count].suffix = suffix;
    ctx->count++;
}

static void arad_parse_serials(arad_mm_ctx_t *ctx, char *list)
{
    char *tok;
    char *saveptr = NULL;

    if (!ctx || !list)
        return;

    tok = arad_strtok(list, ";", &saveptr);
    while (tok) {
        char *dash;
        uint32_t ser = 0;
        uint32_t suf = 0;

        tok  = arad_trim(tok);
        dash = tok ? strchr(tok, '-') : NULL;

        if (tok && *tok) {
            if (dash) {
                *dash = '\0';
                if (arad_parse_u32(arad_trim(tok), &ser) &&
                        arad_parse_u32(arad_trim(dash + 1), &suf) &&
                        suf <= 0xffu) {
                    arad_add_serial(ctx, ser, (int)suf);
                }
            }
            else if (arad_parse_u32(tok, &ser)) {
                arad_add_serial(ctx, ser, -1);
            }
        }

        tok = arad_strtok(NULL, ";", &saveptr);
    }
}

static int arad_match_serial(const arad_mm_ctx_t *ctx, uint32_t ser24, uint8_t suffix)
{
    unsigned i;

    if (!ctx || ctx->count == 0)
        return 1;

    ser24 &= 0x00ffffffu;
    for (i = 0; i < ctx->count; ++i) {
        if (ctx->serials[i].ser24 != ser24)
            continue;
        if (ctx->serials[i].suffix < 0 || (uint8_t)ctx->serials[i].suffix == suffix)
            return 1;
    }
    return 0;
}

static int arad_parse_gear(const char *s, float *out)
{
    if (!s || !out)
        return 0;

    if (strcmp(s, "0.01") == 0) {
        *out = 0.01f;
        return 1;
    }
    if (strcmp(s, "0.1") == 0) {
        *out = 0.1f;
        return 1;
    }
    if (strcmp(s, "1") == 0 || strcmp(s, "1.0") == 0) {
        *out = 1.0f;
        return 1;
    }
    if (strcmp(s, "10") == 0 || strcmp(s, "10.0") == 0) {
        *out = 10.0f;
        return 1;
    }
    if (strcmp(s, "100") == 0 || strcmp(s, "100.0") == 0) {
        *out = 100.0f;
        return 1;
    }
    return 0;
}

static int arad_parse_unit(const char *s, arad_unit_t *out)
{
    if (!s || !out)
        return 0;

    if (arad_ieq(s, "m3")) {
        *out = ARAD_UNIT_M3;
        return 1;
    }
    if (arad_ieq(s, "l") || arad_ieq(s, "liter") || arad_ieq(s, "liters")) {
        *out = ARAD_UNIT_L;
        return 1;
    }
    if (arad_ieq(s, "cf") || arad_ieq(s, "cuft") || arad_ieq(s, "cu_ft")) {
        *out = ARAD_UNIT_CF;
        return 1;
    }
    if (arad_ieq(s, "usg") || arad_ieq(s, "gal") || arad_ieq(s, "gallon") || arad_ieq(s, "gallons")) {
        *out = ARAD_UNIT_USG;
        return 1;
    }
    return 0;
}

static const char *arad_unit_str(arad_unit_t unit)
{
    switch (unit) {
    case ARAD_UNIT_M3: return "m3";
    case ARAD_UNIT_L: return "l";
    case ARAD_UNIT_CF: return "cu ft";
    case ARAD_UNIT_USG: return "gal";
    default: return "m3";
    }
}

static uint64_t const arad_xor_table[] = {
        0xa0b5486480, 0xd05ac3a0d8, 0x682da64b08, 0xb416732c78,
        0x5a0bfe0d58, 0xad055f0f50, 0xc68a887978, 0x634583a7d8,
        0xa1aa21b658, 0x40dd972c00, 0xa06eac0498, 0x5037919928,
        0xa81b68c568, 0xd40d146b48, 0xea062a3c58, 0x7503d28548,
        0xaa89092710, 0xd544e30110, 0x7aaa31ecc0, 0x3d5518f660,
        0x8ea2ab85e0, 0x475155c2f0, 0xb3a08d1fa8, 0x49d8c178f8,
        0x34e4e74b50, 0x1a7273a5a8, 0x0d39fe49b0, 0x9694d8da08,
        0x4b4aabf660, 0x35ad159778, 0x8ade6aae08, 0x456ff2cc60,
        0xb2bfde98e0, 0xd95f88dee8, 0xfca7240ac0, 0xfe53f597f8,
        0xff295ac200, 0xef9c8a9fd0, 0x67c60523a0, 0x23eb42fd98,
        0x81fd411b78, 0xd0f640e808, 0x687be7ef60, 0xb43d946528,
        0xda1e6a3b68, 0x6d0ff286d0, 0xa68fdebdb8, 0xd3474f5720,
        0xf9ab805540, 0xecdde7d470, 0xf66e9478a0, 0x7b374a3c50,
        0xad9382e0f8, 0xc6c12115c8, 0xe360308318, 0x61b89fb6a0,
        0x20d40fb718, 0x106ac040e8, 0x0835a7bb10, 0x841ab44f10,
        0x420d5a2788, 0xa1060d1a38, 0x408b817a30, 0xa045a72f80,
        0xd022b40558, 0x68119d99c8, 0xb4086ec518, 0x5a04f0f9e8,
        0x2d02bfe790, 0x06891f9f80, 0x8344e85d58, 0x51aaf3d980,
        0x38dd398088, 0x9c6e3cc9b8, 0x4e37d9ffb8, 0xa71b4cf620,
        0xc3858185c0, 0xf1cae73c30, 0x68ed33f250, 0xb476fe6bb0,
        0x5a3b7f35d8, 0xad1d1f9310, 0xc686a83758, 0x63439380c8,
        0xa1a929a5d0, 0xc0dcb32c38, 0x606e9e0d78, 0x3037889dd8};

static uint64_t arad_checksum(uint8_t const *b)
{
    uint64_t checksum = 0;
    int n;
    int i;

    for (n = 0; n < 11; n++) {
        for (i = 0; i < 8; i++) {
            if ((b[n] << (i + 1)) & 0x100)
                checksum ^= arad_xor_table[n * 8 + i];
        }
    }
    return checksum;
}

static void arad_flip_payload_bit(uint8_t *b, int bit_index)
{
    int byte_idx    = bit_index / 8;
    int bit_in_byte = bit_index % 8;
    b[byte_idx] ^= (uint8_t)(1u << (7 - bit_in_byte));
}

static int arad_correct_bits(uint8_t *b, uint64_t syndrome)
{
    int i;
    int j;
    int k;

    for (i = 0; i < 88; i++) {
        if (arad_xor_table[i] == syndrome) {
            arad_flip_payload_bit(b, i);
            return 1;
        }
    }

    for (i = 0; i < 88; i++) {
        for (j = i + 1; j < 88; j++) {
            if ((arad_xor_table[i] ^ arad_xor_table[j]) == syndrome) {
                arad_flip_payload_bit(b, i);
                arad_flip_payload_bit(b, j);
                return 2;
            }
        }
    }

    for (i = 0; i < 88; i++) {
        for (j = i + 1; j < 88; j++) {
            uint64_t x = arad_xor_table[i] ^ arad_xor_table[j];
            for (k = j + 1; k < 88; k++) {
                if ((x ^ arad_xor_table[k]) == syndrome) {
                    arad_flip_payload_bit(b, i);
                    arad_flip_payload_bit(b, j);
                    arad_flip_payload_bit(b, k);
                    return 3;
                }
            }
        }
    }

    return -1;
}

extern r_device const arad_ms_meter;

static r_device *arad_ms_meter_create(char *args)
{
    r_device *dev = decoder_create(&arad_ms_meter, sizeof(arad_mm_ctx_t));
    arad_mm_ctx_t *ctx;
    char *work;
    char *tok;
    char *saveptr = NULL;

    if (!dev)
        return NULL;

    ctx                 = (arad_mm_ctx_t *)decoder_user_data(dev);
    ctx->count          = 0;
    ctx->user_gear_set  = 0;
    ctx->user_gear      = 0.1f;
    ctx->user_units_set = 0;
    ctx->user_units     = ARAD_UNIT_M3;

    if (!args)
        return dev;

    args = arad_trim(args);
    if (!*args)
        return dev;

    work = strdup(args);
    if (!work)
        return dev;

    tok = arad_strtok(work, ",:", &saveptr);
    while (tok) {
        char *eq;
        char *key;
        char *val;

        tok = arad_trim(tok);
        eq  = strchr(tok, '=');
        if (!eq) {
            tok = arad_strtok(NULL, ",:", &saveptr);
            continue;
        }

        *eq = '\0';
        key = arad_trim(tok);
        val = arad_trim(eq + 1);

        if (arad_ieq(key, "serial") || arad_ieq(key, "serials")) {
            arad_parse_serials(ctx, val);
        }
        else if (arad_ieq(key, "gear")) {
            float g;
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

        tok = arad_strtok(NULL, ",:", &saveptr);
    }

    free(work);
    return dev;
}

/**
Dialog3G decoder with checksum MIC.

Optional parameters:
- serials=SER1;SER2;SER3-SUFFIX
- gear=0.01|0.1|1|10|100
- units=m3|l|cf|usg

The serial filter is optional.
Gear and units may be overridden when auto detection is not reliable enough.
Up to 3 payload bit errors are corrected using the checksum syndrome.


See notes in https://45851052.fs1.hubspotusercontent-na1.net/hubfs/45851052/documents/files/Interpreter-II-Register_v0710.20F.pdf
and https://www.arad.co.il/wp-content/uploads/Dialog-3G-register-information-sheet_Eng-002.pdf

S.a. issues #1992, #3459

Programmable parameters:
- Meter User ID:
  A municipal Meter ID number of up to 5 digits (16 or 17 bits needed)
- Transponder No:
  Meter’s Dialog 3GTM transponder number of up to 12 digits (40 bits needed)
- Reading:
  The transmitted Dialog 3G TM meter reading (up to 9 digits), (30 bits needed)
  the accumulated and the display readout are always equivalent.
- Meter Type:
  Meter type such as water, gas or electricity
- Count Factor:
  Meter count unit. It is a pre scale factor which is initially programmed
  into the Dialog 3G TM unit is order to get the standaed measurement units
  for the system billing, management and calculation (Gallons or Cubic/ Mettic)
- Alarms Temper:
  A warning temper sign, in case of unauthorized meter tampering. CCW: Reverse
  consumption by the meter.
- Gear Ratio:
  Water meter mechanical gear ratio parameter for the 3G Interpreter register types

Programmable registration includes USG, CF, or M3, while
resolution of the flow multiplier provides a custom-tailored
enhanced display (.01, 0.1, 1, 10, 100).

RF information:
- FSK Manchester, ISM 915 Mhz
- Message is being sent once every 30 seconds.

Data Layout, payload in square brackets:

    Byte Position                                 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16
    Bit Position used for XOR                   [0  8  16 24 32 40 48 56 64 72 80]
    Sample         00 00 00 00 3e 69 0a ec 7a c8 4b 47 f7 2e 00 40 5f 25 00 00 0c 9e c5 cb 55 38 f8
                   00 00 00 00 UU UU UU UU UU UU[FF SS SS SS LL UU CC CC CC ?? ?F]OO OO OO OO OO TT

- 00:  {?} Preamble
- UU: {48} UID, sync word, always 0x3e690aec7ac8
- FF:  {8} Flags, mostly 0x4b, but 0x6b in case of water leak
- SS: {24} little-endian, serial number, decimal value on the water counter
- LL:  {8} Serial suffix, mostly 0x73 = "s", 0x27 ="'" , or 0x00 = No Letter at all. Used for gear and volume units.
- UU:  {8} Gear/scale and volume units flags, mostly 0x00 or 0x40
           if b[4] = 0x73 and b[5] = 0x00           then gear = 0.1,  native_unit = m3
           if b[4] = 0x00 and (b[5] = 0x00 or 0x40) then gear = 0.01, native_unit = m3
           if b[4] = 0x27 and b[5] = 0x00           then gear = 0.1,  native_unit = US Gallon
           other combinaisons are unknown

- CC: {24} little-endian, counter value
- ??: {12} Always 0x000, the first 8 bit could be counter MSB
-  F:  {4} Flags, mostly 0x5, but could be 0x0, 0x8 or 0xC, impacted in case of leak and depends also on the serial suffix and units values.
            if b[0] = 0x4b and b[4] = 0x00 or = 0x73       then Flags = 0x5
            if b[0] = 0x4b and b[5] = 0x40 or = 0x50       then Flags = 0xC
            if b[0] = 0x4b and b[4] = 0x27 and b[5] = 0x00 then Flags = 0x0
            if b[0] = 0x6b and b[5] = 0x00 or = 0x40       then Flags = 0x8, LEAK here

- OO: {40} Checksum, XOR 40 bit masks of the payload data (11 byte x 8 bit) = 88 different masks, see xor_table bellow, 3 bit errors could be corrected.
- TT:  {8} Trailing suffix byte, mostly 0xff or 0xf8

XOR masks by bit and byte positions, 88 masks in total (bit from left to right):

    0xa0b5486480  [ bit  0 byte  0 ], 0xd05ac3a0d8  [ bit  1 byte  0 ], 0x682da64b08  [ bit  2 byte  0 ], 0xb416732c78  [ bit  3 byte  0 ],
    0x5a0bfe0d58  [ bit  4 byte  0 ], 0xad055f0f50  [ bit  5 byte  0 ], 0xc68a887978  [ bit  6 byte  0 ], 0x634583a7d8  [ bit  7 byte  0 ],
    0xa1aa21b658  [ bit  8 byte  1 ], 0x40dd972c00  [ bit  9 byte  1 ], 0xa06eac0498  [ bit 10 byte  1 ], 0x5037919928  [ bit 11 byte  1 ],
    0xa81b68c568  [ bit 12 byte  1 ], 0xd40d146b48  [ bit 13 byte  1 ], 0xea062a3c58  [ bit 14 byte  1 ], 0x7503d28548  [ bit 15 byte  1 ],
    0xaa89092710  [ bit 16 byte  2 ], 0xd544e30110  [ bit 17 byte  2 ], 0x7aaa31ecc0  [ bit 18 byte  2 ], 0x3d5518f660  [ bit 19 byte  2 ],
    0x8ea2ab85e0  [ bit 20 byte  2 ], 0x475155c2f0  [ bit 21 byte  2 ], 0xb3a08d1fa8  [ bit 22 byte  2 ], 0x49d8c178f8  [ bit 23 byte  2 ],
    0x34e4e74b50  [ bit 24 byte  3 ], 0x1a7273a5a8  [ bit 25 byte  3 ], 0x0d39fe49b0  [ bit 26 byte  3 ], 0x9694d8da08  [ bit 27 byte  3 ],
    0x4b4aabf660  [ bit 28 byte  3 ], 0x35ad159778  [ bit 29 byte  3 ], 0x8ade6aae08  [ bit 30 byte  3 ], 0x456ff2cc60  [ bit 31 byte  3 ],
    0xb2bfde98e0  [ bit 32 byte  4 ], 0xd95f88dee8  [ bit 33 byte  4 ], 0xfca7240ac0  [ bit 34 byte  4 ], 0xfe53f597f8  [ bit 35 byte  4 ],
    0xff295ac200  [ bit 36 byte  4 ], 0xef9c8a9fd0  [ bit 37 byte  4 ], 0x67c60523a0  [ bit 38 byte  4 ], 0x23eb42fd98  [ bit 39 byte  4 ],
    0x81fd411b78  [ bit 40 byte  5 ], 0xd0f640e808  [ bit 41 byte  5 ], 0x687be7ef60  [ bit 42 byte  5 ], 0xb43d946528  [ bit 43 byte  5 ],
    0xda1e6a3b68  [ bit 44 byte  5 ], 0x6d0ff286d0  [ bit 45 byte  5 ], 0xa68fdebdb8  [ bit 46 byte  5 ], 0xd3474f5720  [ bit 47 byte  5 ],
    0xf9ab805540  [ bit 48 byte  6 ], 0xecdde7d470  [ bit 49 byte  6 ], 0xf66e9478a0  [ bit 50 byte  6 ], 0x7b374a3c50  [ bit 51 byte  6 ],
    0xad9382e0f8  [ bit 52 byte  6 ], 0xc6c12115c8  [ bit 53 byte  6 ], 0xe360308318  [ bit 54 byte  6 ], 0x61b89fb6a0  [ bit 55 byte  6 ],
    0x20d40fb718  [ bit 56 byte  7 ], 0x106ac040e8  [ bit 57 byte  7 ], 0x0835a7bb10  [ bit 58 byte  7 ], 0x841ab44f10  [ bit 59 byte  7 ],
    0x420d5a2788  [ bit 60 byte  7 ], 0xa1060d1a38  [ bit 61 byte  7 ], 0x408b817a30  [ bit 62 byte  7 ], 0xa045a72f80  [ bit 63 byte  7 ],
    0xd022b40558  [ bit 64 byte  8 ], 0x68119d99c8  [ bit 65 byte  8 ], 0xb4086ec518  [ bit 66 byte  8 ], 0x5a04f0f9e8  [ bit 67 byte  8 ],
    0x2d02bfe790  [ bit 68 byte  8 ], 0x06891f9f80  [ bit 69 byte  8 ], 0x8344e85d58  [ bit 70 byte  8 ], 0x51aaf3d980  [ bit 71 byte  8 ],
    0x38dd398088  [ bit 72 byte  9 ], 0x9c6e3cc9b8  [ bit 73 byte  9 ], 0x4e37d9ffb8  [ bit 74 byte  9 ], 0xa71b4cf620  [ bit 75 byte  9 ],
    0xc3858185c0  [ bit 76 byte  9 ], 0xf1cae73c30  [ bit 77 byte  9 ], 0x68ed33f250  [ bit 78 byte  9 ], 0xb476fe6bb0  [ bit 79 byte  9 ],
    0x5a3b7f35d8  [ bit 80 byte 10 ], 0xad1d1f9310  [ bit 81 byte 10 ], 0xc686a83758  [ bit 82 byte 10 ], 0x63439380c8  [ bit 83 byte 10 ],
    0xa1a929a5d0  [ bit 84 byte 10 ], 0xc0dcb32c38  [ bit 85 byte 10 ], 0x606e9e0d78  [ bit 86 byte 10 ], 0x3037889dd8  [ bit 87 byte 10 ]

Format string:

    UID:48h FLAGS:8h SERIAL: <24d c BILLING UNIT:8h COUNTER: <32d FLAGS:8h XOR:40h SUFFIX:hh



*/
static int arad_mm_dialog3g_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[]            = {0xf5, 0x13, 0x85, 0x37}; //Full preamble (inverted polarity): 96 f5 13 85 37 b4
    unsigned const preamble_pattern_bits        = 32;
    unsigned const preamble_pattern_offset_bits = 8; // f5 starts 8 bits after full preamble start
    arad_mm_ctx_t *ctx                          = (arad_mm_ctx_t *)decoder_user_data(decoder);
    uint8_t b[16];
    uint8_t u[7];
    uint64_t xor_raw;
    uint64_t xor_cal;
    char uid_str[15];
    char sernoout[12];
    int row = 0;
    unsigned match_pos;
    int full_preamble_start;
    unsigned payload_start;
    unsigned uid_bits;
    int corrections = 0;
    int leaking;
    uint32_t serno;
    uint32_t wreadraw;
    uint8_t sn_sufx;
    uint8_t flags1;
    uint8_t flags2;
    arad_unit_t unit = ARAD_UNIT_M3;
    float scale      = 0.1f;
    float volume;

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }

    if (bitbuffer->bits_per_row[row] < 18 * 8)
        return DECODE_ABORT_LENGTH;

    match_pos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, preamble_pattern_bits);
    if (match_pos + preamble_pattern_bits > bitbuffer->bits_per_row[row]) {
        decoder_log(decoder, 2, __func__, "Sync word not found");
        return DECODE_ABORT_LENGTH;
    }

    full_preamble_start = (int)match_pos - (int)preamble_pattern_offset_bits;
    if (full_preamble_start < 0)
        return DECODE_ABORT_EARLY;

    payload_start = (unsigned)full_preamble_start + 48;
    if (payload_start + 128 > bitbuffer->bits_per_row[row])
        return DECODE_ABORT_LENGTH;

    uid_bits = payload_start - (unsigned)full_preamble_start;
    if (uid_bits > sizeof(u) * 8)
        uid_bits = sizeof(u) * 8;

    bitbuffer_invert(bitbuffer);

    bitbuffer_extract_bytes(bitbuffer, row, (unsigned)full_preamble_start, u, uid_bits);
    bitrow_snprint(u, uid_bits, uid_str, sizeof(uid_str));

    bitbuffer_extract_bytes(bitbuffer, row, payload_start, b, 128);
    decoder_log_bitrow(decoder, 2, __func__, b, 128, "MSG Extracted");

    xor_raw = ((uint64_t)b[11] << 32) | ((uint64_t)b[12] << 24) | ((uint64_t)b[13] << 16) | ((uint64_t)b[14] << 8) | b[15];
    xor_cal = arad_checksum(b);
    if (xor_raw != xor_cal) {
        corrections = arad_correct_bits(b, xor_raw ^ xor_cal);
        if (corrections < 0)
            return DECODE_FAIL_MIC;
    }

    leaking  = (b[0] & 0x20) >> 5;
    serno    = (uint32_t)b[1] | ((uint32_t)b[2] << 8) | ((uint32_t)b[3] << 16);
    sn_sufx  = b[4];
    flags1   = b[5];
    wreadraw = (uint32_t)b[6] | ((uint32_t)b[7] << 8) | ((uint32_t)b[8] << 16);
    flags2   = b[10];

    if (sn_sufx == 0x00 && (flags1 == 0x00 || flags1 == 0x40))
        scale = 0.01f;
    else if (sn_sufx == 0x27 && flags1 == 0x00)
        unit = ARAD_UNIT_USG;

    if (ctx && !arad_match_serial(ctx, serno, sn_sufx))
        return DECODE_ABORT_EARLY;

    if (ctx && ctx->user_gear_set)
        scale = ctx->user_gear;
    if (ctx && ctx->user_units_set)
        unit = ctx->user_units;

    volume = (float)wreadraw * scale;

    snprintf(sernoout, sizeof(sernoout), "%08u-%02x", (unsigned)serno, (unsigned)sn_sufx);

    /* clang-format off */
    data_t *data = data_make(
            "model",       "",              DATA_STRING, "AradMsMeter-Dialog3G",
            "id",          "Serial No",     DATA_STRING, sernoout,
            "uid",         "UID",           DATA_STRING, uid_str,
            "leaking",     "Leaking",       DATA_INT, leaking,
            "flags1",      "Flags 1",       DATA_FORMAT, "%02x", DATA_INT, flags1,
            "volume",      "Volume",        DATA_DOUBLE, volume,
            "unit",        "Unit",          DATA_STRING, arad_unit_str(unit),
            "flags2",      "Flags 2",       DATA_FORMAT, "%02x", DATA_INT, flags2,
            "mic",         "Integrity",     DATA_STRING, corrections > 0 ? "CHECKSUM-CORR" : "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "uid",
        "leaking",
        "flags1",
        "volume",
        "unit",
        "flags2",
        "mic",
        NULL,
};

r_device const arad_ms_meter = {
        .name        = "Arad/Master Meter Dialog3G water utility meter",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 8.4,
        .long_width  = 8.4,
        .reset_limit = 100,
        .decode_fn   = &arad_mm_dialog3g_decode,
        .create_fn   = &arad_ms_meter_create,
        .fields      = output_fields,
};
