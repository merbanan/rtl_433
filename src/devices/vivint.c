/** @file
    Vivint security sensors (345 MHz).

    Copyright (C) 2026 Benjamin Larsson <banan@ludd.ltu.se>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "bit_util.h"
#include "data.h"
#include "decoder.h"
#include "r_device.h"

#include <openssl/crypto.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define VIVINT_MSG_BIT_LEN 80
#ifndef VIVINT_MAX_SENSORS
#define VIVINT_MAX_SENSORS 32
#endif
#ifndef VIVINT_CACHED_COUNTERS
#define VIVINT_CACHED_COUNTERS 8
#endif
#ifndef VIVINT_SEED_DATA_REQUIRED
#define VIVINT_SEED_DATA_REQUIRED 6
#endif
#if VIVINT_SEED_DATA_REQUIRED < 1 || VIVINT_SEED_DATA_REQUIRED > VIVINT_CACHED_COUNTERS
#error "VIVINT_SEED_DATA_REQUIRED must be between 1 and VIVINT_CACHED_COUNTERS"
#endif

#define VIVINT_ENTRY_COUNTER      0x17
#define VIVINT_RABBIT_CIPHER_SIZE 48

#define CHANNEL_VIVINT            0x70
#define CHANNEL_POWERON           0xd0
#define CHANNEL_SKEY              0xe0
#define CHANNEL_LEGACY            0xf0

/* Some devices will send a packet without encrypted status data
 This could be a change in device configuration or some devices
 may just not use the encryted options */
/* The following values only apply to vivint devices */
#define VIVINT_EVENT_UNENCRYPTED 0x01
#define VIVINT_PACKET_BATTERY    0x02
#define VIVINT_PACKET_SEED       0x03
// For some reason the flood sensor will send out a 0x74 packet when the WPS/Pair button is pressed
// It looks like b[1] is the temperature in C when the flood sensor sends a 0x74. Additionally, the
// bit that is cleared when the packet is encrypted is set, signifying a change in format where the
// counter should be.
#define VIVINT_EVENT_PIR       0x04
#define VIVINT_PACKET_MFG_BOOT 0x06
#define VIVINT_EVENT_FLOOD     0x07
#define VIVINT_EVENT_GB        0x09
#define VIVINT_EVENT_DW        0x0a

/* This bit in b[3] can change the meaning of a packet to include values */
#define VIVINT_UNENCRYPTED_FLAG_BIT 0x02

/* Sent with the 0xd0 packet */
#define VIVINT_DEVICE_TYPE_V_PIR2_345    0x0a
#define VIVINT_DEVICE_TYPE_VS_SKEY2_345  0x0b
#define VIVINT_DEVICE_TYPE_VS_FLD001_345 0x0c
#define VIVINT_DEVICE_TYPE_V_DW21R_345   0x0f
#define VIVINT_DEVICE_TYPE_V_GB2_345     0x14
#define VIVINT_DEVICE_TYPE_V_DW11_345    0x18

// Include seed and seed-discovery diagnostics in decoder output by default.
// Define OUTPUT_VIVINT_DECODE=0 to omit these fields from emitted data.
#ifndef OUTPUT_VIVINT_DECODE
#define OUTPUT_VIVINT_DECODE 1
#endif

/**
Vivint security sensors (345.0 MHz).

@attention stateful
This decoder stores data from a number of packets in order to collect enough
data to determine a 16 bit unique seed value that each sensor uses as a base
to encode certain packets

Tested with the Vivint V-DW21R-345 and V-DW11-345 door/window sensors,
as well as with the V-PIR2-345 and V-GB2-345 devices.

Tested with the VS-FLD001-345, but need more data to confirm packet
formats. The tested version does not use packet encryption

This can capture the 0xd0 message from the VS-SKEY2-345 but can not
decode the button presses.

OOK Manchester (zerobit), 0xFFFE preamble, 96 bit (12 byte) packet. Decoded
payload (80 data bits, 10 bytes) after the preamble:

    For the 0xd0 messages:

    TT BB NN BB II II II II RR RR

    For the 0x74, 0x79, and 0x7a messages:

    TT CC CC FF II II II II RR RR

    For the 0x72 messages the format is device dependent

    For the 0x73 messages:

    TT SS SS 02 II II II II RR RR




- T: 8 bit frame subtype:
     0x71 = Unencrypted event
     0x72 = Battery voltage and threshold
     0x73 = Raw seed value
     0x74 = PIR motion
     0x76 = Used during manufacturing bringup
     0x79 = GB glass-break
     0x7a = DW door/window
     0xd0 = power-on/startup beacon

- N: 8 bit constant used to identify the type of device
     0x0a: V-PIR2-345
     0x0b: VS-SKEY2-345
     0x0c: VS-FLD001-345
     0x0f: V-DW21R-345
     0x14: V-GB2-345
     0x18: V-DW11-345
     Other unknown device values are the V-PIR3-345, V-GB3-345, keyfobs, flood, and some other sensors

- B: 16 bit value that gives a device specific battery level

- C: 16 bit counter, increments every transmission
- F: 8 bit status byte. The low 2 bits are always zero for a standard encrypted message; the rest (including
     bit 7, open/closed for 0x7a) are XORed with a per-device keystream,
     see below
- I: 32 bit device identifier
- R: 16 bit CRC

- S: 16 bit raw seed value

I is the sensor's printed TXID: split into a 12 bit and a 20 bit decimal
number, e.g. 0x0137beda -> 19, 507610 -> "0019-0507610" (label
"0019-050-7610"). Exposed as the `id` field. Same scheme as Honeywell/2GIG
(issue #1261).

Non-0xd0 subtypes use a packed 12-bit CRC:
  - CRC-16 poly 0x8050 over b[0..7] + top_nibble(b[8])  (9 bytes)
  - check12 = crc16 >> 4; stored12 = (low_nibble(b[8]) << 8) | b[9]
  - valid when check12 == stored12

0xd0 frames use standard CRC-16 poly 0x8050 over b[0..7].

A 0xd0 frame is sent with a packet counter of 24 which happens every 65kish
packets or upon powerup because the packet counter is reset to 24 on every boot

Per-device keystream (F field, 0x7a, 0x74, 0x79 frames only):

Each sensor has a 16 bit per-device seed, that under unknown circumstances,
can be transmitted over the air, that keys a Rabbit stream cipher core
(RFC 4503) advancing every transmission (keyed off the 16 bit counter) to
produce a keystream byte XORed into F; bit 7 of the decrypted byte is the
Loop 1, which for a DW21R is the door state (1 = open). The high nibble of
the first CRC byte carries an authentication value of `(c3 ^ 0x10) & 0xf0`.

The decoder can discover the 16 bit seed by collecting frames with distinct
packet counters. Once VIVINT_SEED_DATA_REQUIRED samples are available (six by
default), it searches the seed space using their authentication nibbles. If the
result is not unique, later frames replace older samples and the search is
retried. Seed discovery state is reported in `decode_status`,
`seed_data_count`, `seed_data_required`, and `seed_candidate_count`.

A known seed can be supplied to decrypt F immediately. rtl_433 accepts a
comma-separated TXID-to-seed list as a runtime decoder argument:

    rtl_433 -R 342:0019-0507610=05c9,0019-0507743=dda9

rtl_433_ESP accepts the same list through the compile-time VIVINT_SEEDS
definition and can omit seed diagnostics with OUTPUT_VIVINT_DECODE=0:

    '-DVIVINT_SEEDS="0019-0507610=05c9,0019-0507743=dda9"'
    '-DOUTPUT_VIVINT_DECODE=0'

The runtime argument takes precedence when both are present. Without either,
automatic seed discovery remains enabled.

When OUTPUT_VIVINT_DECODE is enabled (the default), a configured or discovered
seed is reported in `seed` as a four-character hexadecimal string along with
the seed-discovery status fields; set it to 0 to omit them. Valid seeds and
authentication nibbles emit the decrypted `state`, `loop1`, `tamper`, `loop2`,
`loop3`, `battery_low`, and `heartbeat` fields. Otherwise, `data` contains the
raw payload.

For example, create `vivint-defines.cmake` in the source directory to supply a
seed and disable its diagnostic fields:

    set(CMAKE_C_FLAGS
        "${CMAKE_C_FLAGS} -DOUTPUT_VIVINT_DECODE=0 -DVIVINT_SEED_DATA_REQUIRED=6 -DVIVINT_SEEDS=\\\"0016-0357170=c283\\\""
        CACHE STRING "Custom Vivint compiler definitions" FORCE)

Then configure and build normally:

    cmake -S . -B build -G Ninja -C vivint-defines.cmake
    cmake --build build -j4

See https://github.com/merbanan/rtl_433/issues/1504
*/

/* Rabbit stream cipher core (RFC 4503). Inner state: eight 32-bit state
   words X0..X7, eight 32-bit counter words C0..C7,
   modeled as a flat byte window. Custom to this protocol (not part of
   RFC 4503): a 16-bit seed in place of Rabbit's 128-bit key/IV, and a
   per-packet schedule keyed off the transmission counter. */
typedef struct {
    uint32_t C[8];
    uint32_t X[8];
    uint16_t S[8];
    uint16_t K[8];
    uint32_t b;
} vivint_rabbit_t;

/* Rabbit concat two 16 bit values */
static uint32_t vivint_rabbit_concat(uint16_t a, uint16_t b)
{
    return (((uint32_t)a) << 16) | (uint32_t)b;
}

/* Rabbit rotate a 32bit value to the left */
static uint32_t vivint_rotl32(uint32_t x, unsigned n)
{
    return (x << n) | (x >> (32 - n));
}

/* Base of the rabbit cipher function 'g' */
static uint32_t vivint_rabbit_g(uint32_t u, uint32_t v)
{
    uint64_t sq = u + v;
    sq          = sq * sq;
    return ((sq >> 32) & 0x00000000ffffffff) ^ (sq & 0x00000000ffffffff);
}

/* Expands the 16-bit seed into the 8 words vivint_rabbit_key_setup()
   The magic values are pulled from sensor firmwares. */
static void vivint_expand_key(vivint_rabbit_t *g, uint16_t seed)
{
    uint16_t base = seed ^ 0x0008;
    g->K[0]       = base;
    g->K[1]       = (uint16_t)(base + 0x25);
    g->K[2]       = (uint16_t)(base - 0x04);
    g->K[3]       = (uint16_t)(base + 0x2c);
    g->K[4]       = (uint16_t)(base - 0x09);
    g->K[5]       = (uint16_t)(base - 0x1d);
    g->K[6]       = base ^ 0x00f9;
    g->K[7]       = base ^ 0x0022;
}

/* Derives X0..X7 and C0..C7 (RFC 4503 SS2.2) from the 8 seed-expanded words,
   analogous to RFC 4503 SS2.3 key setup but with this protocol's own
   permutation, re-derived from the counter each call. */
static void vivint_rabbit_key_setup(vivint_rabbit_t *g)
{
    /* 2.3.  Key Setup Scheme

     The counter carry bit b is initialized to zero.  The state and
     counter words are derived from the key K[127..0].

     The key is divided into subkeys K0 = K[15..0], K1 = K[31..16], ... K7
     = K[127..112].  The initial state is initialized as follows:

       for j=0 to 7:
         if j is even:
           Xj = K(j+1 mod 8) || Kj
           Cj = K(j+4 mod 8) || K(j+5 mod 8)
         else:
           Xj = K(j+5 mod 8) || K(j+4 mod 8)
           Cj = Kj || K(j+1 mod 8)
    */

    g->b = 0;
    for (int j = 0u; j < 8; j++) {
        if (j % 2 == 0) {
            /* EVEN */
            g->X[j] = vivint_rabbit_concat(g->K[(j + 1) % 8], g->K[j]);
            g->C[j] = vivint_rabbit_concat(g->K[(j + 4) % 8], g->K[(j + 5) % 8]);
        }
        else {
            /* ODD */
            g->X[j] = vivint_rabbit_concat(g->K[(j + 5) % 8], g->K[(j + 4) % 8]);
            g->C[j] = vivint_rabbit_concat(g->K[j], g->K[(j + 1) % 8]);
        }
    }
}

static void vivint_rabbit_update_counters(vivint_rabbit_t *g)
{
    /* 2.5.  Counter System

     Before each execution of the next-state function (Section 2.6), the
     counter system has to be updated.  This system uses constants
     A1,...,A7, as follows:

     A0 = 0x4D34D34D         A1 = 0xD34D34D3
     A2 = 0x34D34D34         A3 = 0x4D34D34D
     A4 = 0xD34D34D3         A5 = 0x34D34D34
     A6 = 0x4D34D34D         A7 = 0xD34D34D3

     It also uses the counter carry bit b to update the counter system, as
     follows:

     for j=0 to 7:
      temp = Cj + Aj + b
      b    = temp div WORDSIZE
      Cj   = temp mod WORDSIZE

     Note that on exiting this loop, the variable b has to be preserved
     for the next iteration of the system.  */

    const uint32_t A[8] = {0x4D34D34D, 0xD34D34D3, 0x34D34D34, 0x4D34D34D, 0xD34D34D3, 0x34D34D34, 0x4D34D34D, 0xD34D34D3};

    for (int j = 0u; j < 8; j++) {
        uint64_t temp = (uint64_t)(g->C[j]) + (uint64_t)(A[j]) + (uint64_t)(g->b);
        g->b          = temp / 0x100000000;
        g->C[j]       = temp & 0xffffffff;
    }
}

/* Counter update (RFC 4503 SS2.5) and next-state function (RFC 4503 SS2.6).
   The g-function's 64-bit square is built from 16-bit-limb partial
   products. */
static void vivint_rabbit_next_state(vivint_rabbit_t *g)
{
    /* 2.6.  Next-State Function

      The core of the Rabbit algorithm is the next-state function.  It is
      based on the function g, which transforms two 32-bit inputs into one
      32-bit output, as follows:

        g(u,v) = LSW(square(u+v)) ^ MSW(square(u+v))

      where square(u+v) = ((u+v mod WORDSIZE) * (u+v mod WORDSIZE)).

      Using this function, the algorithm updates the inner state as
      follows:

        for j=0 to 7:
          Gj = g(Xj,Cj)

    */

    uint32_t G[8];
    for (int j = 0u; j < 8; j++) {
        G[j] = vivint_rabbit_g(g->X[j], g->C[j]);
    }

    // X0 = G0 + (G7 <<< 16) + (G6 <<< 16) mod WORDSIZE
    g->X[0] = G[0] + vivint_rotl32(G[7], 16) + vivint_rotl32(G[6], 16);
    // X1 = G1 + (G0 <<<  8) +  G7         mod WORDSIZE
    g->X[1] = G[1] + vivint_rotl32(G[0], 8) + G[7];
    // X2 = G2 + (G1 <<< 16) + (G0 <<< 16) mod WORDSIZE
    g->X[2] = G[2] + vivint_rotl32(G[1], 16) + vivint_rotl32(G[0], 16);
    // X3 = G3 + (G2 <<<  8) +  G1         mod WORDSIZE
    g->X[3] = G[3] + vivint_rotl32(G[2], 8) + G[1];
    // X4 = G4 + (G3 <<< 16) + (G2 <<< 16) mod WORDSIZE
    g->X[4] = G[4] + vivint_rotl32(G[3], 16) + vivint_rotl32(G[2], 16);
    // X5 = G5 + (G4 <<<  8) +  G3         mod WORDSIZE
    g->X[5] = G[5] + vivint_rotl32(G[4], 8) + G[3];
    // X6 = G6 + (G5 <<< 16) + (G4 <<< 16) mod WORDSIZE
    g->X[6] = G[6] + vivint_rotl32(G[5], 16) + vivint_rotl32(G[4], 16);
    // X7 = G7 + (G6 <<<  8) +  G5         mod WORDSIZE
    g->X[7] = G[7] + vivint_rotl32(G[6], 8) + G[5];
}

/* Function used during Rabbit stream generation after setup */
static void vivint_rabbit_reinitialize_counters(vivint_rabbit_t *g)
{
    for (int j = 0u; j < 8; j++) {
        g->C[j] ^= g->X[(j + 4) % 8];
    }
}

/* The MSP430s and MCUs on the vivint sensors do not have enough
 * RAM and rather than extracting the full 16 byte secret every
 * cycle, grabs a byte based off of the packet counter and
 * caculates them on demand. In this application, we can extract
 * the full 48 byte cipher stream. */
static void vivint_rabbit_extract(vivint_rabbit_t *g)
{
    //  2.7.  Extraction Scheme

    //  After the key and IV setup are concluded, the algorithm is iterated
    //  in order to produce one 128-bit output block, S, per round.  Each
    //  round consists of executing steps 2.5 and 2.6 and then extracting an
    //  output S[127..0] as follows:

    //    S[15..0]    = X0[15..0]  ^ X5[31..16]
    g->S[0] = (g->X[0] & 0xffff) ^ ((g->X[5] >> 16) & 0xffff);
    //    S[31..16]   = X0[31..16] ^ X3[15..0]
    g->S[1] = ((g->X[0] >> 16) & 0xffff) ^ (g->X[3] & 0xffff);
    //    S[47..32]   = X2[15..0]  ^ X7[31..16]
    g->S[2] = (g->X[2] & 0xffff) ^ ((g->X[7] >> 16) & 0xffff);
    //    S[63..48]   = X2[31..16] ^ X5[15..0]
    g->S[3] = ((g->X[2] >> 16) & 0xffff) ^ (g->X[5] & 0xffff);
    //    S[79..64]   = X4[15..0]  ^ X1[31..16]
    g->S[4] = (g->X[4] & 0xffff) ^ ((g->X[1] >> 16) & 0xffff);
    //    S[95..80]   = X4[31..16] ^ X7[15..0]
    g->S[5] = ((g->X[4] >> 16) & 0xffff) ^ (g->X[7] & 0xffff);
    //    S[111..96]  = X6[15..0]  ^ X3[31..16]
    g->S[6] = (g->X[6] & 0xffff) ^ ((g->X[3] >> 16) & 0xffff);
    //    S[127..112] = X6[31..16] ^ X1[15..0]
    g->S[7] = ((g->X[6] >> 16) & 0xffff) ^ (g->X[1] & 0xffff);
}

/* Generate the 48 bytes of cipher given a specific key
   out should be a uint8_t[48] */
static void vivint_rabbit_gen_cipher(vivint_rabbit_t *g, uint8_t *out)
{

    vivint_rabbit_key_setup(g);

    // Iterate 4 times before extracting secrets
    for (int i = 0; i < 4; i++) {
        vivint_rabbit_update_counters(g);
        vivint_rabbit_next_state(g);
    }
    // Necessary step before extracting the secrets
    vivint_rabbit_reinitialize_counters(g);

    // Now we pull out all 48 bytes of data for the cipher
    // auto out = ret.begin();
    for (int i = 0; i < 3; i++) {

        vivint_rabbit_update_counters(g);
        vivint_rabbit_next_state(g);
        vivint_rabbit_extract(g);

        for (int j = 0; j < 8; j++) {
            *out = g->S[j] & 0x00ff;
            out++;
            *out = (g->S[j] >> 8) & 0x00ff;
            out++;
        }
    }
}

/* Customization to the rabbit cipher that modifies the 128bit key based on the packet counter
 * This makes the key seem like it is more than 16 bits */
static void vivint_gen_rabbit_key(vivint_rabbit_t *g, uint16_t seed, uint16_t counter)
{
    uint16_t i = 24;
    do {
        if (i > 0xfff7) {
            i = 0;
        }
        if (i == 24) {
            vivint_expand_key(g, seed);
        }
        int mod   = i % 7;
        g->K[mod] = i + mod + g->K[mod];
        g->K[7]   = g->K[7] ^ mod;
        i += 12;
    } while (i <= counter);
}

/* Per-device decode state: advanced incrementally as packets with
   increasing counters arrive, re-synced from the entry counter on a
   backward jump (sensor power-cycle). */
typedef struct {
    uint32_t id;
    uint16_t seed;
    uint16_t last_counter;
    uint8_t cipher[VIVINT_RABBIT_CIPHER_SIZE];
    int counter_idx;
    int seed_matches;
    uint16_t counters[VIVINT_CACHED_COUNTERS];
    uint8_t cipher_cache[VIVINT_CACHED_COUNTERS];
} vivint_sensor_t;

typedef struct {
    unsigned count;
    vivint_sensor_t sensors[VIVINT_MAX_SENSORS];
} vivint_ctx_t;

static vivint_sensor_t *vivint_ctx_find(vivint_ctx_t *ctx, uint32_t id)
{
    if (!ctx) {
        return NULL;
    }
    for (unsigned i = 0; i < ctx->count; ++i) {
        if (ctx->sensors[i].id == id) {
            return &ctx->sensors[i];
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

/* Parses a comma-separated "NNNN-NNNNNNN=hexseed" list (the same TXID
   format this decoder prints). The list can come from rtl_433's standard
   runtime decoder argument or rtl_433_ESP's VIVINT_SEEDS definition. */
static r_device *vivint_create(char const *args)
{
    r_device *dev = decoder_create(&vivint, sizeof(vivint_ctx_t));
    if (!dev) {
        return NULL;
    }

    vivint_ctx_t *ctx = (vivint_ctx_t *)decoder_user_data(dev);
    ctx->count        = 0;

#ifdef VIVINT_SEEDS
    // Preserve rtl_433's runtime decoder argument as the preferred method.
    // ESP32 has no command-line registration, so fall back to the compile-time
    // seed list only when the caller supplied no runtime argument.
    if (!args || !*args) {
        args = VIVINT_SEEDS;
    }
#endif

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
        if (ctx->count < VIVINT_MAX_SENSORS && sscanf(tok, "%u-%u=%x", &p1, &p2, &seed) == 3) {
            vivint_sensor_t *s = &ctx->sensors[ctx->count++];
            s->id              = ((p1 & 0xfff) << 20) | (p2 & 0xfffff);
            s->seed            = (uint16_t)seed;
        }
        tok = vivint_strtok(NULL, ",", &saveptr);
    }

    free(work);
    return dev;
}

// Generates the cipher values given a specific seed and counter
static void vivint_rabbit_advance_cipher(vivint_sensor_t *s, uint16_t counter)
{
    // Need to check to see if we need to regenerate our cipher output
    int diff = counter - s->last_counter;
    if ((s->last_counter == 0xffff) ||
            (counter % 12 == 0) || (diff > 12) || (diff < -12) ||
            (counter % 12 < s->last_counter % 12)) {
        // We need to regenerate
        // Could make static to take it off of the stack
        vivint_rabbit_t rabbit;
        vivint_gen_rabbit_key(&rabbit, s->seed, counter);
        vivint_rabbit_gen_cipher(&rabbit, s->cipher);
    }
    s->last_counter = counter;
}

/* The other 4 bits of bytes 8 and 9 in the data contain 4 bits of the raw cipher data xor'd with 0x10 */
static int vivint_validate_rabbit_nibble(vivint_sensor_t *s, uint8_t check, uint16_t counter)
{
    int mod          = counter % 12;
    uint8_t mac_byte = s->cipher[mod * 4 + 2] & 0xf0;

    return mac_byte == (check ^ 0x10);
}

/* Returns the decrypted message data */
static int vivint_decrypt_flags(vivint_sensor_t *s, int flags, uint16_t counter)
{
    int mod = counter % 12;

    int decrypt_byte = s->cipher[mod * 4];

    // The last 2 bits are always 0 in encrypted messages
    return (flags ^ decrypt_byte) & 0xfc;
}

/* Iterates through cached data from a sensor to determine the seed */
static int vivint_determine_seed(r_device *decoder, vivint_sensor_t *s)
{
    int num_matches       = 0;
    uint16_t matched_seed = 0xffff;
    for (uint16_t seed = 1; seed < 0xffff; seed++) {
        s->last_counter = 0xffff;
        for (int i = 0; i < VIVINT_SEED_DATA_REQUIRED; i++) {
            int idx = (s->counter_idx - VIVINT_SEED_DATA_REQUIRED + i) % VIVINT_CACHED_COUNTERS;
            s->seed = seed;
            vivint_rabbit_advance_cipher(s, s->counters[idx]);
            if (!vivint_validate_rabbit_nibble(s, s->cipher_cache[idx], s->counters[idx])) {
                break;
            }
            if (i == VIVINT_SEED_DATA_REQUIRED - 1) {
                matched_seed = seed;
                num_matches++;
            }
        }
    }

    s->seed_matches = num_matches;

    if (num_matches == 1) {
        decoder_logf(decoder, 1, __func__, "Determined seed: %x", (unsigned)matched_seed);
        s->seed = matched_seed;
        // Reset the last counter so it regenerates the cipher with the new key
        s->last_counter = 0xffff;
        vivint_rabbit_advance_cipher(s, s->counters[(s->counter_idx - 1) % VIVINT_CACHED_COUNTERS]);
        return 1;
    }
    else {
        s->seed = 0xffff;
        return 0;
    }
}

static int vivint_decode_poweron(r_device *decoder, uint8_t *b)
{
    int crc           = (b[8] << 8) | b[9];
    int model_type    = b[2];
    int battery_level = ((b[3] << 4) | b[1]) & 0x0fff;
    // Contains the full 32bit TXID
    int id = ((unsigned)b[4] << 24) | ((unsigned)b[5] << 16) | ((unsigned)b[6] << 8) | b[7];
    char id_str[13];
    snprintf(id_str, sizeof(id_str), "%04u-%07u", (id >> 20) & 0xfff, id & 0xfffff);

    const char *model;
    switch (model_type) {
    case VIVINT_DEVICE_TYPE_VS_FLD001_345:
        model = "Vivint Security VS-FLD001-345";
        break;
    case VIVINT_DEVICE_TYPE_VS_SKEY2_345:
        model = "Vivint Security VS-SKEY2-345";
        break;
    case VIVINT_DEVICE_TYPE_V_DW11_345:
        model = "Vivint Security V-DW11-345";
        break;
    case VIVINT_DEVICE_TYPE_V_DW21R_345:
        model = "Vivint Security V-DW21R-345";
        break;
    case VIVINT_DEVICE_TYPE_V_GB2_345:
        model = "Vivint Security V-GB2-345";
        break;
    case VIVINT_DEVICE_TYPE_V_PIR2_345:
        model = "Vivint Security V-PIR2-345";
        break;
    default:
        model = "Vivint Security";
        break;
    }

    if (crc != crc16(b, 8, 0x8050, 0)) {
        decoder_logf(decoder, 2, __func__, "CRC check failed");
        return DECODE_FAIL_MIC;
    }

    /* clang-format off */
    data_t *data = data_make(
            "id",              "TXID",          DATA_STRING, id_str,
            "event",           "Event",         DATA_STRING, "power_on",
            "model",           "",              DATA_STRING, model,
            "battery_level",   "",              DATA_INT, battery_level,
            "mic",             "Integrity",     DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

/* This packet is device specific, and contains stuff like counter for RC circuits and register settings for voltage monitors on MSP430s */
static int vivint_decode_battery(r_device *decoder, uint8_t *b, const char *id_str)
{
    /* This might not be true for all device types */
    int bat_level     = ((b[1] << 4) & 0x0ff0) | b[2] >> 4;
    int bat_threshold = ((b[2] & 0x0f) << 4) | (b[3] >> 2);

    /* clang-format off */
    data_t *data = data_make(
            "model",             "",              DATA_STRING, "Vivint Security",
            "id",                "TXID",          DATA_STRING, id_str,
            "event",             "Event",         DATA_STRING, "battery",
            "battery_level",     "",              DATA_INT, bat_level,
            "battery_threshold", "",              DATA_INT, bat_threshold,
            "mic",               "Integrity",     DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

/* For devices that are not yet included in this decoder.
 * If you have a device that generates this type of message,
 * consider creating an issue with some captured data */
static int vivint_decode_unknown(r_device *decoder, uint8_t *b)
{
    char payload[21];

    for (int i = 0; i < 10; ++i) {
        snprintf(&payload[i * 2], 3, "%02x", b[i]);
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",             "",              DATA_STRING, "Vivint Security",
            "id",                "",              DATA_STRING, "0000-000-0000",
            "event",             "Event",         DATA_STRING, "unknown",
            "data",              "",              DATA_STRING,  payload,
            "mic",               "Integrity",     DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

/* Sent while upgrading to encrypted communication
 * Unknown exactly what the counter means, but once it
 * reaches 25, it upgrades the device to encrypted communication.
 * Probably set within 60 seconds of powering on the device with
 * tamper or another input. */
static int vivint_decode_mfg_boot(r_device *decoder, uint8_t *b, const char *id_str)
{
    int counter  = ((b[3] >> 2) & 0x0ff) | (b[1] & 0x0001);
    int val_0xca = b[2];        // Should be 0xca
    int val_0x34 = b[1] & 0xfe; // Should be 0x34

    if (!(b[3] & 0x02) && val_0x34 != 0x34 && val_0xca != 0xca) {
        decoder_logf(decoder, 2, __func__, "Non-conforming mfg boot packet format");
        return DECODE_FAIL_SANITY;
    }

    char payload[21];
    for (int i = 0; i < 10; ++i) {
        snprintf(&payload[i * 2], 3, "%02x", b[i]);
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",             "",              DATA_STRING, "Vivint Security",
            "id",                "TXID",          DATA_STRING, id_str,
            "counter",           "",              DATA_INT,    counter,
            "event",             "Event",         DATA_STRING, "battery",
            "data",              "",              DATA_STRING,  payload,
            "mic",               "Integrity",     DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

/* Untested, difficult to get devices to trigger this output */
static int vivint_decode_seed(r_device *decoder, uint8_t *b, int id, const char *id_str)
{
    uint8_t two   = b[3];
    uint16_t seed = (b[1] << 8) | b[2]; // Useful data included in packet

    if (two != 0x02) {
        decoder_logf(decoder, 2, __func__, "Non-conforming seed packet format");
        return DECODE_FAIL_SANITY;
    }
    //
    vivint_sensor_t *s = vivint_ctx_find((vivint_ctx_t *)decoder_user_data(decoder), id);
    if (!s) {
        vivint_ctx_t *ctx = (vivint_ctx_t *)decoder_user_data(decoder);
        if (ctx->count < VIVINT_MAX_SENSORS) {
            s               = &ctx->sensors[ctx->count++];
            s->id           = id;
            s->seed         = seed;
            s->last_counter = 0xffff;
            s->counter_idx  = 0;
            s->seed_matches = 1;
        }
    }
    else {
        if (s->seed == 0xffff || s->seed == 0) {
            s->seed = seed;
        }
        else if (s->seed != seed) {
            s->seed = seed;
        }
    }

    char seed_str[5];
    snprintf(seed_str, sizeof(seed_str), "%04x", seed);

    /* clang-format off */
    data_t *data = data_make(
            "model",           "",              DATA_STRING, "Vivint Security",
            "id",              "TXID",          DATA_STRING, id_str,
            "event",           "Event",         DATA_STRING, "seed",
            "seed",            "",              DATA_INT, seed_str,
            "mic",             "Integrity",     DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static int vivint_decode_event(r_device *decoder, uint8_t *b, int id, const char *id_str, int event, int has_encrypted_flags)
{
    int flags         = b[3];        // Useful data included in packet
    int cipher_nibble = b[8] & 0xf0; // For encrypted messages, contains 4 bits of the cipher

    int loop1_bit       = 0; // Loop 1, PIR motion, external contact for DW11, and reed for DW21R
    int tamper_bit      = 0; // Case open tamper
    int loop2_bit       = 0; // Loop 2, or reed for DW11
    int loop3_bit       = 0; // Loop 3, or freeze for FLD001
    int battery_low_bit = 0; // Does the sensor have a low battery voltage?
    int heartbeat_bit   = 0; // Bit that toggles at a timed interval based on sum of 797
    int counter         = 0; // For encrypted packets, we need to track this counter because it tells us where in the cipher the packet is
    int has_valid_flags = 0; // Were the flags appropriately decoded?

    uint16_t seed       = 0xffff;
    int seed_data_count = 0;
    int seed_matches    = -1;

    const char *decode_status = "unknown";
    const char *event_str;

    char payload[21];

    if (has_encrypted_flags) {
        /* The bit[1] of the flags tells if the event data is encrypted or not
         * and is always low when encrypted and contains a counter */
        if (!(flags & VIVINT_UNENCRYPTED_FLAG_BIT)) {
            counter = (b[1] << 8) | b[2];
            // This means that we have the counter and need to work on decrypting
            vivint_sensor_t *s = vivint_ctx_find((vivint_ctx_t *)decoder_user_data(decoder), id);
            if (!s) {
                vivint_ctx_t *ctx = (vivint_ctx_t *)decoder_user_data(decoder);
                if (ctx->count < VIVINT_MAX_SENSORS) {
                    s               = &ctx->sensors[ctx->count++];
                    s->id           = id;
                    s->seed         = 0xffff;
                    s->last_counter = 0xffff;
                    s->counter_idx  = 0;
                    s->seed_matches = -1;
                }
            }
            if (s) {
                decode_status = "collecting_seed";
                // Let's see if we can determine the seed
                if (s->seed == 0xffff || s->seed == 0x0000) {
                    // Store the data we need to determine the seed
                    // Prevent duplicates
                    if (counter != s->last_counter) {
                        int idx              = s->counter_idx % VIVINT_CACHED_COUNTERS;
                        s->cipher_cache[idx] = cipher_nibble;
                        s->counters[idx]     = counter;
                        s->counter_idx++;
                        // Check if we have enough data to determine the seed
                        if (s->counter_idx >= VIVINT_SEED_DATA_REQUIRED) {
                            decoder_logf(decoder, 1, __func__, "Attempting to crack seed");
                            vivint_determine_seed(decoder, s);
                        }
                    }
                    s->last_counter = counter;
                }

                if (s->seed != 0xffff && s->seed != 0x0000) {
                    // This is where we try to decode the message
                    // We also need to check the high nibble of byte 8 to check if
                    // the cipher is correct
                    vivint_rabbit_advance_cipher(s, counter);
                    if (vivint_validate_rabbit_nibble(s, cipher_nibble, counter)) {
                        has_valid_flags = 1;
                        flags           = vivint_decrypt_flags(s, flags, counter);
                    }
                    else {
                        decode_status = "cipher_check_failed";
                        decoder_logf(decoder, 2, __func__, "Invalid Rabbit cipher check nibble");
                    }
                }
                seed            = s->seed;
                seed_data_count = s->counter_idx < VIVINT_CACHED_COUNTERS ? s->counter_idx : VIVINT_CACHED_COUNTERS;
                seed_matches    = s->seed_matches;
                if (has_valid_flags) {
                    decode_status = "decoded";
                }
                else if (seed == 0xffff || seed == 0x0000) {
                    if (seed_matches == 0)
                        decode_status = "seed_not_found";
                    else if (seed_matches > 1)
                        decode_status = "seed_ambiguous";
                }
            }
            else {
                decode_status = "sensor_cache_full";
            }
        }
        else {
            /* Some devices include values here for different messages,
             * for example the VS-FLD001-345 sends a 0x74 message with the current temperature in C. */

            decode_status   = "unencrypted";
            has_valid_flags = 0;
        }
    }
    else {
        decode_status   = "unencrypted";
        has_valid_flags = 1;
    }

#if OUTPUT_VIVINT_DECODE
    char seed_str[5];
    snprintf(seed_str, sizeof(seed_str), "%04x", seed);
#else
    // These values still drive decoder state above, but are not emitted.
    (void)seed_data_count;
    (void)decode_status;
#endif

    switch (event) {
    case VIVINT_EVENT_DW:
        event_str = "door_window_open_close";
        break;
    case VIVINT_EVENT_FLOOD:
        event_str = "flood_heat_freeze";
        break;
    case VIVINT_EVENT_GB:
        event_str = "glass_break";
        break;
    case VIVINT_EVENT_PIR:
        event_str = "motion";
        break;
    default:
        event_str = "unknown";
        break;
    }

    if (has_valid_flags) {
        /* Extract DW11 event bits (1T23BHEZ layout):
                   1=loop1(7), T=tamper(6), 2=loop2(5), 3=loop3(4),
                   B=battery_low(3), H=heartbeat(2), E=Encrypted(1),
                   Z=zero(0) */
        loop1_bit       = flags & 0x80 ? 1 : 0;
        tamper_bit      = flags & 0x40 ? 1 : 0;
        loop2_bit       = flags & 0x20 ? 1 : 0;
        loop3_bit       = flags & 0x10 ? 1 : 0;
        battery_low_bit = flags & 0x08 ? 1 : 0;
        heartbeat_bit   = flags & 0x04 ? 1 : 0;
    }
    else {
        for (int i = 0; i < 10; ++i) {
            snprintf(&payload[i * 2], 3, "%02x", b[i]);
        }
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",              DATA_STRING, "Vivint Security",
            "id",           "TXID",          DATA_STRING, id_str,
            "counter",      "",              DATA_COND, has_valid_flags, DATA_FORMAT, "%04x", DATA_INT, counter,
#if OUTPUT_VIVINT_DECODE
            "seed",         "",              DATA_COND, has_encrypted_flags && seed != 0xffff && seed != 0x0000, DATA_STRING, seed_str,
            "decode_status", "Decode status", DATA_COND, has_encrypted_flags, DATA_STRING, decode_status,
            "seed_data_count", "Seed samples collected", DATA_COND, has_encrypted_flags && (seed == 0xffff || seed == 0x0000), DATA_INT, seed_data_count,
            "seed_data_required", "Seed samples required", DATA_COND, has_encrypted_flags && (seed == 0xffff || seed == 0x0000), DATA_INT, VIVINT_SEED_DATA_REQUIRED,
            "seed_candidate_count", "Seed candidates", DATA_COND, has_encrypted_flags && (seed == 0xffff || seed == 0x0000) && seed_matches >= 0, DATA_INT, seed_matches,
#endif
            "flags",        "",              DATA_COND, has_valid_flags, DATA_FORMAT, "%02x", DATA_INT, flags,
            "event_type",   "",              DATA_FORMAT, "%02x", DATA_INT, event,
            "event",        "Event",         DATA_STRING, event_str,
            "state",        "",              DATA_COND, has_valid_flags,  DATA_STRING,  loop1_bit ? "open" : "closed",
            "loop1",        "",              DATA_COND, has_valid_flags,  DATA_INT,     loop1_bit,
            "tamper",       "",              DATA_COND, has_valid_flags,  DATA_INT,     tamper_bit,
            "loop2",        "",              DATA_COND, has_valid_flags,  DATA_INT,     loop2_bit,
            "loop3",        "",              DATA_COND, has_valid_flags,  DATA_INT,     loop3_bit,
            "battery_low",  "Battery",       DATA_COND, has_valid_flags,  DATA_INT,     battery_low_bit,
            "heartbeat",    "",              DATA_COND, has_valid_flags,  DATA_INT,     heartbeat_bit,
            "data",         "",              DATA_COND, !has_valid_flags, DATA_STRING,  payload,
            "mic",          "Integrity",     DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

/* Base function used by rtl_433 to decode the bit stream */
static int vivint_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[2] = {0xff, 0xe0}; /* 12 bits of 0xFFFE */

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }
    int row = 0;

    decoder_log_bitrow(decoder, 2, __func__, bitbuffer->bb[row], bitbuffer->bits_per_row[row], "MSG");

    bitbuffer_invert(bitbuffer);

    /* This still misses a lot of packets when it catches a high bit at the start of the packet, leading to 0x80000 being seen instead of 0xfffe */
    int pos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, 12) + 12;
    int len = bitbuffer->bits_per_row[row] - pos;
    if (len < VIVINT_MSG_BIT_LEN) {
        decoder_logf(decoder, 2, __func__, "Too short (%d bits after preamble)", len);
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[VIVINT_MSG_BIT_LEN / 8 + 1];
    bitbuffer_extract_bytes(bitbuffer, row, pos, b, VIVINT_MSG_BIT_LEN);
    decoder_log_bitrow(decoder, 2, __func__, b, VIVINT_MSG_BIT_LEN, "MSG (inverted, aligned)");

    int channel = b[0] & 0xf0;
    int event   = b[0] & 0x0f;
    unsigned id = 0;
    char id_str[13];

    if (channel == CHANNEL_POWERON) {
        return vivint_decode_poweron(decoder, b);
    }
    else if ((channel & 0xf0) == CHANNEL_VIVINT) {
        int has_encrypted_flags = 0;

        /* Contains the full 32bit TXID */
        id = ((unsigned)b[4] << 24) | ((unsigned)b[5] << 16) | ((unsigned)b[6] << 8) | b[7];
        snprintf(id_str, sizeof(id_str), "%04u-%07u", (id >> 20) & 0xfff, id & 0xfffff);

        /* Check the CRC. Encrypted messages leak 4 bits of the raw cipher and unencrypted messages
           contain a multiple of 797 in those 4 bits. */
        int crc     = ((b[8] << 8) | b[9]) & 0x0fff;
        int b8_full = b[8];

        b[8] &= 0xf0;
        if (crc != (crc16(b, 9, 0x8050, 0) >> 4)) {
            decoder_logf(decoder, 2, __func__, "CRC check failed");
            return DECODE_FAIL_MIC;
        }
        b[8] = b8_full;

        /* Check if we just have an unencrypted event. The high nibble of b[1] will be the actual event code */
        if (event == VIVINT_EVENT_UNENCRYPTED) {
            /* Check to see if the bit is set in the flags to signify not encrypted */
            event = (b[1] & 0xf0) >> 4;
        }
        else {
            has_encrypted_flags = 1;
        }

        if (event == VIVINT_PACKET_BATTERY) {
            return vivint_decode_battery(decoder, b, id_str);
        }
        else if (event == VIVINT_PACKET_SEED) {
            return vivint_decode_seed(decoder, b, id, id_str);
        }
        else if (event == VIVINT_PACKET_MFG_BOOT) {
            return vivint_decode_mfg_boot(decoder, b, id_str);
        }
        else if (event == VIVINT_EVENT_DW || event == VIVINT_EVENT_GB || event == VIVINT_EVENT_PIR || event == VIVINT_EVENT_FLOOD) {
            return vivint_decode_event(decoder, b, id, id_str, event, has_encrypted_flags);
        }
        else {
            decoder_logf(decoder, 2, __func__, "Unknown event type");
            return DECODE_FAIL_OTHER;
        }
    }
    else if (channel == CHANNEL_SKEY) {
        /* SKEY codes start with 0xe0 and then have the 24bit TXID */
        decoder_logf(decoder, 2, __func__, "Unhandled device messages");
        return DECODE_FAIL_OTHER;
    }
    else {
        /* Check the possible known CRC configurations
           Could change decoding behavior based on which crc is correct */
        int crc = (b[8] << 8) | b[9];

        if (crc != crc16(b, 8, 0x8005, 0) && crc != crc16(b, 8, 0x8050, 0)) {
            crc         = ((b[8] << 8) | b[9]) & 0x0fff;
            int b8_full = b[8];

            b[8] &= 0xf0;
            if (crc != (crc16(b, 9, 0x8050, 0) >> 4) && crc != (crc16(b, 9, 0x8005, 0) >> 4)) {
                decoder_logf(decoder, 2, __func__, "CRC check failed");
                return DECODE_FAIL_MIC;
            }
            b[8] = b8_full;
        }
        return vivint_decode_unknown(decoder, b);
    }

    return DECODE_FAIL_OTHER;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "counter",
#if OUTPUT_VIVINT_DECODE
        "seed",
        "decode_status",
        "seed_data_count",
        "seed_data_required",
        "seed_candidate_count",
#endif
        "flags",
        "event_type",
        "state",
        "loop1",
        "tamper",
        "loop2",
        "loop3",
        "battery_low",
        "heartbeat",
        "data",
        "battery_level",
        "battery_threshold",
        "mic",
        NULL,
};

r_device const vivint = {
        .name        = "Vivint 345MHz Sensors, V-DW11-345/V-DW21R-345, V-GB2-345/V-GB3-345, V-PIR2-345/V-PIR3-345",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 139,
        .long_width  = 0,   /* Not used for manchester encoding */
        .reset_limit = 300, /* Single sensors typically wait 10ms between sending packets */
        .decode_fn   = &vivint_decode,
        .create_fn   = &vivint_create,
        .fields      = output_fields,
};
