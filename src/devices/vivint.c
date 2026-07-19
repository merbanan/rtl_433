/** @file
    Vivint Door/Window Sensors (345 MHz).

    Copyright (C) 2026 Benjamin Larsson <banan@ludd.ltu.se>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIVINT_MSG_BIT_LEN 80
#define VIVINT_MAX_SEEDS 8
#define VIVINT_ENTRY_COUNTER 0x17

/**
Vivint Door/Window Sensors (345.0 MHz).

Tested with the Vivint V-DW21R-345 door/window sensor.

OOK Manchester (zerobit), 0xFFFE preamble, 96 bit (12 byte) packet. Decoded
payload (80 data bits, 10 bytes) after the preamble:

    TT CC CC FF II II II II RR RR

- T: 8 bit frame subtype: 0x7a = DW11 door/window, 0x79 = GB2 glass-break,
     0x74 = PIR2 motion, 0x72/0x73/0x76 = other sensor families,
     0xd0 = power-on/startup beacon
- C: 16 bit counter, increments every transmission
- F: 8 bit status byte. The low 2 bits are always zero; the rest (including
     bit 7, open/closed for 0x7a) are XORed with a per-device keystream,
     see below
- I: 32 bit device identifier
- R: 16 bit CRC

I is the sensor's printed TXID: split into a 12 bit and a 20 bit decimal
number, e.g. 0x0137beda -> 19, 507610 -> "0019-0507610" (label
"0019-050-7610"). Exposed as the `id` field. Same scheme as Honeywell/2GIG
(issue #1261).

Non-0xd0 subtypes use a packed 12-bit CRC:
  - CRC-16 poly 0x8050 over b[0..7] + top_nibble(b[8])  (9 bytes)
  - check12 = crc16 >> 4; stored12 = (low_nibble(b[8]) << 8) | b[9]
  - valid when check12 == stored12

0xd0 frames use standard CRC-16 poly 0x8050 over b[0..7].

Per-device keystream (F field, 0x7a frames only):

Each sensor has a 16 bit per-device seed, not transmitted over the air,
that keys a Rabbit stream cipher core (RFC 4503) advancing every
transmission (keyed off the 16 bit counter) to produce a keystream byte
XORed into F; bit 7 of the decrypted byte is the door contact (1 = open).
The auth nibble in byte 10 of the on-air frame is `(c3 ^ 0x10) & 0xf0`.

The seed cannot be derived from a single frame or from this decoder alone;
it must be obtained externally from several frames at known low counters
(ideally right after a power-cycle). Once known, supply it at registration
time to decrypt F:

    rtl_433 -R 342:0019-0507610=05c9,0019-0507743=dda9

Without a matching seed, `state`/`contact_open` are omitted and the raw
payload is reported in `data` instead.

See https://github.com/merbanan/rtl_433/issues/1504
*/

/* Rabbit stream cipher core (RFC 4503). Inner state: eight 32-bit state
   words X0..X7 at 0x232.. and eight 32-bit counter words C0..C7 at 0x252..,
   modeled as a flat byte window. Custom to this protocol (not part of
   RFC 4503): a 16-bit seed in place of Rabbit's 128-bit key/IV, and a
   per-packet schedule keyed off the transmission counter. */
typedef struct {
    uint8_t m[0x300];
} vivint_rabbit_t;

static uint16_t vivint_rabbit_r16(vivint_rabbit_t *g, unsigned a)
{
    return g->m[a] | ((uint16_t)g->m[a + 1] << 8);
}

static void vivint_rabbit_w16(vivint_rabbit_t *g, unsigned a, uint16_t v)
{
    g->m[a]     = (uint8_t)v;
    g->m[a + 1] = (uint8_t)(v >> 8);
}

static uint32_t vivint_rabbit_r32(vivint_rabbit_t *g, unsigned a)
{
    return (uint32_t)vivint_rabbit_r16(g, a) | ((uint32_t)vivint_rabbit_r16(g, a + 2) << 16);
}

static void vivint_rabbit_w32(vivint_rabbit_t *g, unsigned a, uint32_t v)
{
    vivint_rabbit_w16(g, a, (uint16_t)(v & 0xffff));
    vivint_rabbit_w16(g, a + 2, (uint16_t)(v >> 16));
}

static uint32_t vivint_rotl32(uint32_t x, unsigned n)
{
    return (x << n) | (x >> (32 - n));
}

/* RFC 4503 SS2.5 counter-update constants A0..A7. */
static uint32_t const VIVINT_RABBIT_A[8] = {
        0x4D34D34D, 0xD34D34D3, 0x34D34D34, 0x4D34D34D,
        0xD34D34D3, 0x34D34D34, 0x4D34D34D, 0xD34D34D3,
};

/* Expands the 16-bit seed into the 8 words vivint_rabbit_key_setup()
   interleaves into X and C. */
static void vivint_expand(uint16_t seed, uint16_t out[8])
{
    uint16_t base = seed ^ 0x0008;
    out[0]        = base;
    out[1]        = (uint16_t)(base + 0x25);
    out[2]        = (uint16_t)(base - 0x04);
    out[3]        = (uint16_t)(base + 0x2c);
    out[4]        = (uint16_t)(base - 0x09);
    out[5]        = (uint16_t)(base - 0x1d);
    out[6]        = base ^ 0x00f9;
    out[7]        = base ^ 0x0022;
}

/* Derives X0..X7 and C0..C7 (RFC 4503 SS2.2) from the 8 seed-expanded words,
   analogous to RFC 4503 SS2.3 key setup but with this protocol's own
   permutation, re-derived from the counter each call. */
static void vivint_rabbit_key_setup(vivint_rabbit_t *g)
{
    uint16_t counter = vivint_rabbit_r16(g, 0x206);
    unsigned m       = counter % 7;
    vivint_rabbit_w16(g, 0x27a + m * 2, (uint16_t)(vivint_rabbit_r16(g, 0x27a + m * 2) + counter + m));
    vivint_rabbit_w16(g, 0x288, vivint_rabbit_r16(g, 0x288) ^ (uint16_t)m);

    uint16_t e[8];
    for (int i = 0; i < 8; ++i)
        e[i] = vivint_rabbit_r16(g, 0x27a + 2 * i);

    uint16_t x_words[16];
    uint16_t c_words[16];
    for (int r = 0; r < 8; ++r) {
        if (r % 2 == 0) {
            x_words[2 * r]     = e[r];
            x_words[2 * r + 1] = e[(r + 1) % 8];
            c_words[2 * r]     = e[(r + 5) % 8];
            c_words[2 * r + 1] = e[(r + 4) % 8];
        }
        else {
            x_words[2 * r]     = e[(r + 4) % 8];
            x_words[2 * r + 1] = e[(r + 5) % 8];
            c_words[2 * r]     = e[(r + 1) % 8];
            c_words[2 * r + 1] = e[r];
        }
    }
    for (int i = 0; i < 16; ++i) {
        vivint_rabbit_w16(g, 0x232 + 2 * i, x_words[i]);
        vivint_rabbit_w16(g, 0x252 + 2 * i, c_words[i]);
    }
}

/* Counter update (RFC 4503 SS2.5) and next-state function (RFC 4503 SS2.6).
   The g-function's 64-bit square is built from 16-bit-limb partial
   products. */
static void vivint_rabbit_next_state(vivint_rabbit_t *g)
{
    unsigned const scratch = 0x294;

    for (int r8 = 0; r8 < 8; ++r8) {
        uint16_t lo = vivint_rabbit_r16(g, 0x252 + r8 * 4);
        uint16_t hi = vivint_rabbit_r16(g, 0x254 + r8 * 4);
        vivint_rabbit_w16(g, scratch + r8 * 4, lo);
        vivint_rabbit_w16(g, scratch + 2 + r8 * 4, hi);
    }

    uint32_t lcg = vivint_rabbit_r32(g, 0x272) + VIVINT_RABBIT_A[0];
    vivint_rabbit_w32(g, 0x252, vivint_rabbit_r32(g, 0x252) + lcg);
    for (int r8 = 1; r8 < 8; ++r8) {
        uint32_t a      = vivint_rabbit_r32(g, 0x252 + r8 * 4);
        uint32_t b      = vivint_rabbit_r32(g, 0x24e + r8 * 4);
        uint32_t sub    = vivint_rabbit_r32(g, scratch - 4 + r8 * 4);
        uint32_t borrow = (b < sub) ? 1 : 0;
        vivint_rabbit_w32(g, 0x252 + r8 * 4, a + VIVINT_RABBIT_A[r8] + borrow);
    }

    uint32_t borrow = (vivint_rabbit_r32(g, 0x26e) < vivint_rabbit_r32(g, 0x2b0)) ? 1 : 0;
    vivint_rabbit_w16(g, 0x272, (uint16_t)borrow);
    vivint_rabbit_w16(g, 0x274, 0);

    for (int r8 = 0; r8 < 8; ++r8) {
        uint32_t x   = vivint_rabbit_r32(g, 0x232 + r8 * 4) + vivint_rabbit_r32(g, 0x252 + r8 * 4);
        uint32_t lo  = x & 0xffff;
        uint32_t hi  = x >> 16;
        uint32_t xsq = x * x;
        uint32_t acc = (lo * lo >> 16) >> 1;
        acc          = acc + lo * hi;
        acc >>= 15;
        acc = acc + hi * hi;
        acc ^= xsq;
        vivint_rabbit_w32(g, scratch + r8 * 4, acc);
    }

    int r11        = 7;
    int r10        = 6;
    int r8s[4]     = {0, 2, 4, 6};
    for (int i = 0; i < 4; ++i) {
        int r8      = r8s[i];
        uint32_t t1 = vivint_rotl32(vivint_rabbit_r32(g, scratch + r11 * 4), 16);
        uint32_t t2 = vivint_rotl32(vivint_rabbit_r32(g, scratch + r10 * 4), 16);
        vivint_rabbit_w32(g, 0x232 + r8 * 4, t1 + vivint_rabbit_r32(g, scratch + r8 * 4) + t2);
        r11 = (r11 + 1) % 8;
        r10 = (r10 + 1) % 8;
        uint32_t t3 = vivint_rotl32(vivint_rabbit_r32(g, scratch + r11 * 4), 8);
        vivint_rabbit_w32(g, 0x236 + r8 * 4, t3 + vivint_rabbit_r32(g, scratch + 4 + r8 * 4) + vivint_rabbit_r32(g, scratch + r10 * 4));
        r11 = (r11 + 1) % 8;
        r10 = (r10 + 1) % 8;
    }
}

/* RFC 4503 SS2.3 post-key-setup counter reinitialization: Cj ^= X(j+4 mod 8). */
static void vivint_rabbit_counter_remix(vivint_rabbit_t *g)
{
    for (int r10 = 0; r10 < 8; ++r10) {
        int r11 = r10 * 4;
        int r14 = ((r10 + 4) % 8) * 4;
        vivint_rabbit_w16(g, 0x252 + r11, vivint_rabbit_r16(g, 0x252 + r11) ^ vivint_rabbit_r16(g, 0x232 + r14));
        vivint_rabbit_w16(g, 0x254 + r11, vivint_rabbit_r16(g, 0x254 + r11) ^ vivint_rabbit_r16(g, 0x234 + r14));
    }
}

/* Reduced extraction: RFC 4503 SS2.7 derives a full 128 bit block; this
   protocol only needs the status keystream byte (c1) and the auth byte
   (c3), selecting a different X-word combination per counter mod 4. */
static void vivint_rabbit_extract(vivint_rabbit_t *g)
{
    unsigned k = vivint_rabbit_r16(g, 0x206) & 3;
    uint16_t r14;
    uint16_t r12;
    uint16_t r13;
    switch (k) {
    case 0:
        r14 = vivint_rabbit_r16(g, 0x23e);
        r12 = vivint_rabbit_r16(g, 0x248) ^ vivint_rabbit_r16(g, 0x232);
        r13 = vivint_rabbit_r16(g, 0x234);
        break;
    case 1:
        r14 = vivint_rabbit_r16(g, 0x246);
        r12 = vivint_rabbit_r16(g, 0x250) ^ vivint_rabbit_r16(g, 0x23a);
        r13 = vivint_rabbit_r16(g, 0x23c);
        break;
    case 2:
        r14 = vivint_rabbit_r16(g, 0x24e);
        r12 = vivint_rabbit_r16(g, 0x238) ^ vivint_rabbit_r16(g, 0x242);
        r13 = vivint_rabbit_r16(g, 0x244);
        break;
    default:
        r14 = vivint_rabbit_r16(g, 0x236);
        r12 = vivint_rabbit_r16(g, 0x240) ^ vivint_rabbit_r16(g, 0x24a);
        r13 = vivint_rabbit_r16(g, 0x24c);
        break;
    }
    r13 ^= r14;
    g->m[0x2c1] = (uint8_t)r12;
    g->m[0x2c2] = (uint8_t)(r12 >> 8);
    g->m[0x2c3] = (uint8_t)r13;
    g->m[0x2c4] = (uint8_t)(r13 >> 8);
}

/* Full reseed: key setup, 4 rounds of next-state, counter remix, one more
   next-state, then extract. Run every 12th counter tick. */
static void vivint_rabbit_reseed(vivint_rabbit_t *g)
{
    vivint_rabbit_w16(g, 0x272, 0);
    vivint_rabbit_w16(g, 0x274, 0);
    vivint_rabbit_key_setup(g);
    for (int i = 0; i < 4; ++i)
        vivint_rabbit_next_state(g);
    vivint_rabbit_counter_remix(g);
    vivint_rabbit_next_state(g);
    vivint_rabbit_extract(g);
}

static void vivint_rabbit_begin(vivint_rabbit_t *g, uint16_t seed)
{
    uint16_t init[8];
    memset(g->m, 0, sizeof(g->m));
    vivint_expand(seed, init);
    for (int i = 0; i < 8; ++i)
        vivint_rabbit_w16(g, 0x27a + 2 * i, init[i]);
}

/* Advances one transmit; returns the new counter and status-key (c1) byte. */
static uint16_t vivint_rabbit_tick(vivint_rabbit_t *g, uint16_t counter, uint8_t *c1_out)
{
    counter = (counter == 0xfff7) ? 0 : (uint16_t)(counter + 1);
    vivint_rabbit_w16(g, 0x206, counter);
    if (counter % 12 == 0) {
        vivint_rabbit_reseed(g);
    }
    else if (counter % 4 == 0) {
        vivint_rabbit_next_state(g);
        vivint_rabbit_extract(g);
    }
    else {
        vivint_rabbit_extract(g);
    }
    *c1_out = g->m[0x2c1];
    return counter;
}

/* Per-device decode state: advanced incrementally as packets with
   increasing counters arrive, re-synced from the entry counter on a
   backward jump (sensor power-cycle). */
typedef struct {
    uint32_t id;
    uint16_t seed;
    vivint_rabbit_t gen;
    uint16_t counter;
    uint8_t last_c1;
    int has_last_c1;
} vivint_seed_t;

typedef struct {
    unsigned count;
    vivint_seed_t seeds[VIVINT_MAX_SEEDS];
} vivint_ctx_t;

static void vivint_seed_reset(vivint_seed_t *s)
{
    vivint_rabbit_begin(&s->gen, s->seed);
    s->counter     = VIVINT_ENTRY_COUNTER;
    s->has_last_c1 = 0;
}

/* Returns the status-key byte at counter `target`, or -1 if unreachable
   (more than one counter cycle away, or target is the entry counter
   itself, which is never an observed on-air value). */
static int vivint_seed_c1_at(vivint_seed_t *s, uint16_t target)
{
    if (s->has_last_c1 && target == s->counter) {
        return s->last_c1;
    }
    if (target < s->counter) {
        vivint_seed_reset(s);
    }
    unsigned steps = 0;
    while (s->counter != target) {
        uint8_t c1;
        s->counter     = vivint_rabbit_tick(&s->gen, s->counter, &c1);
        s->last_c1     = c1;
        s->has_last_c1 = 1;
        if (s->counter == target) {
            return c1;
        }
        if (++steps > 0x10000) {
            return -1;
        }
    }
    return -1;
}

static vivint_seed_t *vivint_ctx_find(vivint_ctx_t *ctx, uint32_t id)
{
    if (!ctx) {
        return NULL;
    }
    for (unsigned i = 0; i < ctx->count; ++i) {
        if (ctx->seeds[i].id == id) {
            return &ctx->seeds[i];
        }
    }
    return NULL;
}

extern r_device const vivint;

static char *vivint_strtok(char *str, char const *delim, char **saveptr)
{
#ifdef _MSC_VER
    return strtok_s(str, delim, saveptr);
#else
    return strtok_r(str, delim, saveptr);
#endif
}

/* Parses a comma separated "NNNN-NNNNNNN=hexseed" list (the same TXID
   format this decoder prints), e.g. -R N:0019-0507610=05c9,0019-0507743=dda9 */
static r_device *vivint_create(char const *args)
{
    r_device *dev = decoder_create(&vivint, sizeof(vivint_ctx_t));
    if (!dev) {
        return NULL;
    }

    vivint_ctx_t *ctx = (vivint_ctx_t *)decoder_user_data(dev);
    ctx->count        = 0;

    if (!args || !*args) {
        return dev;
    }

    char *work = strdup(args);
    if (!work) {
        return dev;
    }

    char *saveptr = NULL;
    char *tok     = vivint_strtok(work, ",", &saveptr);
    while (tok) {
        unsigned p1;
        unsigned p2;
        unsigned seed;
        if (ctx->count < VIVINT_MAX_SEEDS
                && sscanf(tok, "%u-%u=%x", &p1, &p2, &seed) == 3) {
            vivint_seed_t *s = &ctx->seeds[ctx->count++];
            s->id            = ((p1 & 0xfff) << 20) | (p2 & 0xfffff);
            s->seed          = (uint16_t)seed;
            vivint_seed_reset(s);
        }
        tok = vivint_strtok(NULL, ",", &saveptr);
    }

    free(work);
    return dev;
}

static int vivint_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[2] = {0xff, 0xe0}; /* 12 bits of 0xFFFE */

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }
    int row = 0;

    decoder_log_bitrow(decoder, 2, __func__, bitbuffer->bb[row], bitbuffer->bits_per_row[row], "MSG");

    bitbuffer_invert(bitbuffer);

    int pos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, 12) + 12;
    int len = bitbuffer->bits_per_row[row] - pos;
    if (len < VIVINT_MSG_BIT_LEN) {
        decoder_logf(decoder, 2, __func__, "Too short (%d bits after preamble)", len);
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[VIVINT_MSG_BIT_LEN / 8 + 1];
    bitbuffer_extract_bytes(bitbuffer, row, pos, b, VIVINT_MSG_BIT_LEN);
    decoder_log_bitrow(decoder, 2, __func__, b, VIVINT_MSG_BIT_LEN, "MSG (inverted, aligned)");

    int event_type  = b[0];
    int counter     = (b[1] << 8) | b[2];
    int flags       = b[3];
    unsigned id     = ((unsigned)b[4] << 24) | ((unsigned)b[5] << 16) | ((unsigned)b[6] << 8) | b[7];
    int crc         = (b[8] << 8) | b[9];

    if (id == 0 || id == 0xffffffff) {
        decoder_logf(decoder, 2, __func__, "Id sanity check failed (%08x)", id);
        return DECODE_FAIL_SANITY;
    }

    int crc_valid = 0;
    if (event_type == 0xd0) {
        if (crc == crc16(b, 8, 0x8050, 0)) crc_valid = 1;
    }
    else {
        uint8_t b8_full = b[8];
        b[8] &= 0xF0;
        int crc_full = crc16(b, 9, 0x8050, 0);
        b[8]         = b8_full;
        int check12  = crc_full >> 4;
        int stored12 = ((b8_full & 0x0F) << 8) | b[9];
        if (check12 == stored12) crc_valid = 1;
    }

    if (!crc_valid) {
        decoder_logf(decoder, 2, __func__, "CRC check failed");
        return DECODE_FAIL_MIC;
    }

    char id_str[13];
    snprintf(id_str, sizeof(id_str), "%04u-%07u", (id >> 20) & 0xfff, id & 0xfffff);

    int has_contact  = 0;
    int contact_open = 0;
    if (event_type == 0x7a) {
        vivint_seed_t *s = vivint_ctx_find((vivint_ctx_t *)decoder_user_data(decoder), id);
        if (s) {
            int c1 = vivint_seed_c1_at(s, (uint16_t)counter);
            if (c1 >= 0) {
                has_contact  = 1;
                contact_open = (flags ^ c1) & 0x80 ? 1 : 0;
            }
        }
    }

    char payload[21];
    if (!has_contact) {
        for (int i = 0; i < 10; ++i)
            snprintf(&payload[i * 2], 3, "%02x", b[i]);
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",              DATA_STRING, "Vivint-Security",
            "id",           "",              DATA_STRING, id_str,
            "counter",      "",              DATA_FORMAT, "%04x", DATA_INT, counter,
            "flags",        "",              DATA_FORMAT, "%02x", DATA_INT, flags,
            "event_type",   "",              DATA_FORMAT, "%02x", DATA_INT, event_type,
            "state",        "",              DATA_COND, has_contact,  DATA_STRING, contact_open ? "open" : "closed",
            "contact_open", "",              DATA_COND, has_contact,  DATA_INT, contact_open,
            "data",         "",              DATA_COND, !has_contact, DATA_STRING, payload,
            "mic",          "Integrity",     DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "counter",
        "flags",
        "event_type",
        "state",
        "contact_open",
        "data",
        "mic",
        NULL,
};

r_device const vivint = {
        .name        = "Vivint Door/Window Sensor, V-DW21R-345",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 150,
        .long_width  = 0,
        .reset_limit = 300,
        .decode_fn   = &vivint_decode,
        .create_fn   = &vivint_create,
        .fields      = output_fields,
};
