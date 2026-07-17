/** @file
    Chamberlain CWPIRC pir sensor.

    Copyright (C) 2023 Bruno OCTAU
    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Chamberlain CWPIRC pir sensor.
Issue #2582 open by \@kuenkin

This is the webpage of the product itself: https://www.chamberlain.com/ca/cwp-wireless-motion-alert-add-on-sensor/p/CWPIRC

The pir sensor have a learn feature for pairing purpose with the base station up to 8 sensors.

Data layout :

    Byte position                00 01 02 03 04 05 06 07 08 09 10 11 12 13
        55 55 ... 55 55 55 2D D4 00 xx xx xx xx xx 01 yy yy yy yy yy CC CC
       |                  |     |                 |                 |     |
       |               ,--'     |                 |                 |     '--------,
       |Sync           |Preamble|Message 0        |Message 1        |CRC-16/XMODEM |

- Message 0   {48} 00 xx xx xx xx xx, always starting with 0x00
- Message 1   {48} 01 yy yy yy yy yy, always starting with 0x01
- CRC-16XModem{16} cc cc  from 00 to 11 byte

Each 40-bit message (xxxxxxxxxx / yyyyyyyyyy) reuses, bit for bit, the
Security+ 2.0 joint-message permutation from secplus_v2.c (a 4-bit order
nibble, a 4-bit invert nibble, then 30 bits of interleaved triplets) --
an unrelated Chamberlain product's payload encoding, reused verbatim
inside this different FHSS transport. Confirmed against many real
transmissions: the resulting 40-bit "fixed" value stays constant per
physical sensor while a 28-bit rolling counter (9+9 base-3 trits split
across both halves) changes every transmission. Bit 5 of fixed is a
low-battery flag: the same sensor's fixed value flips only that bit
when reporting low battery, with 4 consecutive low-battery reports
decoding to consecutive rolling values.
*/

// mirrors secplus_v2.c's invert table (Security+ 2.0 payload permutation)
static int cwpirc_invert(int v, int *inv0, int *inv1, int *inv2)
{
    switch (v) {
    case 0x00: *inv0 = 1; *inv1 = 1; *inv2 = 0; break;
    case 0x01: *inv0 = 0; *inv1 = 1; *inv2 = 0; break;
    case 0x02: *inv0 = 0; *inv1 = 0; *inv2 = 1; break;
    case 0x04: *inv0 = 1; *inv1 = 1; *inv2 = 1; break;
    case 0x05:
    case 0x0a: *inv0 = 1; *inv1 = 0; *inv2 = 1; break;
    case 0x06: *inv0 = 0; *inv1 = 1; *inv2 = 1; break;
    case 0x08: *inv0 = 1; *inv1 = 0; *inv2 = 0; break;
    case 0x09: *inv0 = 0; *inv1 = 0; *inv2 = 0; break;
    default: return -1;
    }
    return 0;
}

// mirrors secplus_v2.c's order table
static int cwpirc_order(int v, int *o0, int *o1, int *o2)
{
    switch (v) {
    case 0x06:
    case 0x09: *o0 = 2; *o1 = 1; *o2 = 0; break;
    case 0x08:
    case 0x04: *o0 = 1; *o1 = 2; *o2 = 0; break;
    case 0x01: *o0 = 2; *o1 = 0; *o2 = 1; break;
    case 0x00: *o0 = 0; *o1 = 2; *o2 = 1; break;
    case 0x05: *o0 = 1; *o1 = 0; *o2 = 2; break;
    case 0x02:
    case 0x0a: *o0 = 0; *o1 = 1; *o2 = 2; break;
    default: return -1;
    }
    return 0;
}

// Decodes one 40-bit message half (Security+ 2.0 joint-message permutation,
// see secplus_v2_decode_v2_half() in secplus_v2.c). roll[9] gets 9 base-3
// rolling trits, *fixed20 gets the 20-bit per-half fixed value. Returns 0
// on success, -1 if the order/invert nibble or a rolling trit is invalid.
static int cwpirc_half_decode(uint8_t const *h, uint8_t roll[9], int *fixed20)
{
    uint64_t h40 = ((uint64_t)h[0] << 32) | ((uint64_t)h[1] << 24)
            | ((uint64_t)h[2] << 16) | ((uint64_t)h[3] << 8) | h[4];

    int order_invert = (int)((h40 >> 30) & 0xff);
    int order        = order_invert >> 4;
    int invert       = order_invert & 0x0f;
    uint32_t x       = (uint32_t)(h40 & 0x3fffffff); // 30 bit interleaved data

    int p0 = 0, p1 = 0, p2 = 0;
    for (int i = 0; i < 10; ++i) {
        p2 ^= (x & 1) << i; x >>= 1;
        p1 ^= (x & 1) << i; x >>= 1;
        p0 ^= (x & 1) << i; x >>= 1;
    }

    int inv0, inv1, inv2;
    if (cwpirc_invert(invert, &inv0, &inv1, &inv2) < 0) {
        return -1;
    }
    if (inv0) p0 = (~p0) & 0x3ff;
    if (inv1) p1 = (~p1) & 0x3ff;
    if (inv2) p2 = (~p2) & 0x3ff;

    int vals[3] = {p0, p1, p2};
    int o0, o1, o2;
    if (cwpirc_order(order, &o0, &o1, &o2) < 0) {
        return -1;
    }
    p0 = vals[o0];
    p1 = vals[o1];
    p2 = vals[o2];

    for (int i = 0; i < 4; ++i) {
        roll[i] = (order_invert >> (6 - 2 * i)) & 0x03;
        if (roll[i] == 3) {
            return -1;
        }
    }
    for (int i = 0; i < 5; ++i) {
        roll[4 + i] = (p2 >> (8 - 2 * i)) & 0x03;
        if (roll[4 + i] == 3) {
            return -1;
        }
    }

    *fixed20 = (p0 << 10) | p1;
    return 0;
}

static int chamberlain_cwpirc_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x55, 0x2D, 0xD4};

    if (bitbuffer->num_rows != 1) {
        decoder_logf(decoder, 2, __func__, "Expected 1 Row, here %d", bitbuffer->num_rows);
        return DECODE_ABORT_EARLY;
    }

    unsigned bits = bitbuffer->bits_per_row[0];

    if (bits < 136 ) {                 // too small
        decoder_logf(decoder, 2, __func__, "less than 136 bits, %d is too short", bits);
        return DECODE_ABORT_LENGTH;
    }

    unsigned search_pos = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof (preamble) * 8);

    if (search_pos >= bits) {
        decoder_logf(decoder, 2, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }

    uint8_t b[14];
    int got_frame = 0;

    // the demod occasionally lands the preamble match a bit or two off from
    // the true frame start; brute-force nearby offsets, gated by the CRC
    for (int shift = 0; shift <= 4; ++shift) {
        unsigned pos = search_pos + sizeof(preamble) * 8 + shift;
        if (pos + sizeof(b) * 8 > bits) {
            break;
        }
        bitbuffer_extract_bytes(bitbuffer, 0, pos, b, sizeof(b) * 8);

        if (b[0] != 0 || b[6] != 1) {
            continue;
        }
        if (crc16(b, 14, 0x1021, 0x0000) != 0) {
            continue;
        }
        got_frame = 1;
        break;
    }

    if (!got_frame) {
        decoder_log(decoder, 2, __func__, "Message 0/1 not found or CRC error");
        return DECODE_FAIL_MIC;
    }

    uint8_t roll0[9], roll1[9];
    int fixed0, fixed1;
    if (cwpirc_half_decode(&b[1], roll0, &fixed0) < 0
            || cwpirc_half_decode(&b[7], roll1, &fixed1) < 0) {
        decoder_log(decoder, 2, __func__, "payload permutation invalid");
        return DECODE_FAIL_SANITY;
    }

    uint64_t fixed        = ((uint64_t)fixed0 << 20) | (uint64_t)fixed1;
    int battery_low       = (fixed & 0x20) != 0;
    uint64_t canonical_id = fixed & ~(uint64_t)0x20;

    // reassemble the 9+9 base-3 rolling trits into a 28 bit counter
    uint8_t rolling_digits[18];
    rolling_digits[0] = roll1[8];
    rolling_digits[1] = roll0[8];
    memcpy(&rolling_digits[2], &roll1[4], 4);
    memcpy(&rolling_digits[6], &roll0[4], 4);
    memcpy(&rolling_digits[10], &roll1[0], 4);
    memcpy(&rolling_digits[14], &roll0[0], 4);

    uint32_t rolling_temp = 0;
    for (int i = 0; i < 18; ++i) {
        rolling_temp = rolling_temp * 3 + rolling_digits[i];
    }
    uint32_t rolling = reverse32(rolling_temp) >> 4;

    char id_str[17];
    snprintf(id_str, sizeof(id_str), "%010llx", (long long unsigned)canonical_id);

    /* clang-format off */
    data_t *data = data_make(
        "model",       "Model",     DATA_STRING,   "Chamberlain-CWPIRC",
        "id",          "",          DATA_STRING,   id_str,
        "battery_ok",  "Battery",   DATA_INT,      !battery_low,
        "rolling",     "Rolling",   DATA_INT,      (int)rolling,
        "mic",         "Integrity", DATA_STRING,   "CRC",
        NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;

}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "rolling",
        "mic",
        NULL,
};

r_device const chamberlain_cwpirc = {
        .name        = "Chamberlain CWPIRC PIR Sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 25,
        .long_width  = 25,
        .reset_limit = 500,
        .decode_fn   = &chamberlain_cwpirc_decode,
        .fields      = output_fields,
};
