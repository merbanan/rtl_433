/** @file
    Elsner Solexa 230V wind/light/temperature handset and sensor.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Elsner Solexa 230V handset and outdoor sensor (wind/temperature/light,
controlling a roller shutter/sunblind).

https://github.com/merbanan/rtl_433/issues/2798

FSK, Manchester coded with a fixed leading zero bit (IEEE 802.3-style,
chip width ~11 us), reported at 868.2 MHz. A long 0xAA-style alternating
preamble is followed by a fixed anchor byte 0x0a, then the on-air frame:

    SSSS SSSS PPPP...PPPP CCCC

- S: 4 byte sync/header region, constant 0xcead93ba on air.
- P: 32 byte payload (raw on-air bytes p0..p31).
- C: CRC-16 (poly 0x1021, init 0x68b3) over the preceding 36 on-air bytes.

Descrambling: the on-air bits are whitened by a self-synchronizing
transform G(x) = x^7 + x^5 + 1 (plain[n] = onair[n]^onair[n-5]^onair[n-7],
MSB first, history zero). After removing it the header becomes the
constant marker 0xc945a400, reported as `id`, and `data` is the resulting
payload (kept as an opaque diagnostic; all further analysis below works on
the raw on-air bytes, not this view).

Rolling state and counter (raw on-air payload, reverse engineered from the
issue's 98 CRC-valid telegrams): bytes p0..p10 are not independent fields,
they are an arithmetic expansion of a single rolling state p0. With

    spread_k(x) = ((x << k) & 0xff) | ((x & 1) ? ((1 << k) - 1) : 0)

every telegram satisfies, universally:

    p3 = (spread_3(p0) + 0x48) & 0xff
    p4 = (spread_4(p0) + 0xd0) & 0xff
    p5 = (spread_5(p0) + 0xa0) & 0xff

and p2/p6/p7 = spread_k(p0) + a family/profile offset. p1 carries a global
transmission sequence counter: p1 = (spread_1(p0) + counter) & 0xff, i.e.
counter = (p1 - spread_1(p0)) & 0xff increments by one per transmission.
p0 and counter are two independent rolling coordinates (a combined 15 bit
value has full affine rank over the examined corpus), not two views of one
counter. This decoder validates the three fixed p3/p4/p5 identities (which
anchor the whole p0 expansion) and reports the rolling state p0 (`rolling`)
and the counter (`counter`). If the identities do not hold the frame is
still output (the CRC-16 is the integrity gate) but those two fields are
omitted and a warning is logged.

Command token: an apparent close/open collision at one repeated rolling
state (where two different button presses produced frames differing only
in the counter-driven p1) initially looked like proof that the button is
absent from the payload; it instead turned out to be two occurrences of the
same filler token (see below), which is why the earlier revision of this
decoder concluded there was no command field. There is one, but only in a
relocated, rolling-state-normalized two byte position:

    L12 = parity(p0 & 0xf9)   L17 = parity(p0 & 0xfe)
    L13 = parity(p0 & 0xfd)   L18 = 1 ^ parity(p0 & 0xff)
    L14 = 1 ^ parity(p0 & 0xff)   L19 = parity(p0 & 0xfe)

    step(prev, base, old, new) = (spread_1(prev) + base + new - old) & 0xff

A one bit selector (`branch1`, bit 2 of the bit-level transform
q[n] = onair[n] XOR onair[n-7] applied to the whole frame, byte q17) picks
which two bytes carry the token: for branch1=0, reconstruct expected bytes
at p13/p14 from p12 with bases 0x6a/0x30; for branch1=1, reconstruct
expected bytes at p18/p19 from p17 with bases 0x30/0x30. The token is the
bytewise difference between the observed and reconstructed bytes. This
collapses all 60 examined telegrams from one handset/receiver pair to
exactly seven values, matching one button/role each:

    token   role
    cc00    close (movement start)
    bb00    open (movement start)
    bd00    stop, or the release packet following an open/close movement
    00ef    automode, variant A
    00e3    automode, variant B
    aac0    filler/status packet (follows open/close/stop; also switch-display)
    a9c0    automode companion packet, also the lone sensor-report telegram

This is reported as `command` only when another one bit indicator
(`family1`, bit 4 of byte q3 of the same transform) reads 1, matching every
telegram from this one handset in the examined corpus; telegrams with
family1=0 (a second population in the issue, of unconfirmed origin) do not
carry a confirmed command in this position and are left unreported. The
token-to-role mapping, and whether the two automode variants are a real
toggle, are established only for this one installation; a second physical
unit would be needed to confirm they are protocol constants rather than a
per-unit or per-firmware detail.

A candidate temperature reading was investigated and rejected: two other
bytes of the same transform, similarly stripped of a p0-derived parity,
read a constant 6.5 C on every examined telegram, but that combination
evaluates to the same constant for all 256 possible values of p0 -- it is
an algebraic identity of the rolling-state formula, not a measurement, so
it is not reported here.

On the sample captures used to verify this decoder, the default FSK pulse
detector mode did not recover any messages -- `-Y minmax` was needed.
*/

// spread_k(x) = ((x<<k)&0xff) | (x.b0 ? (2^k - 1) : 0); see file header.
static uint8_t elsner_spread(uint8_t x, int k)
{
    uint8_t v = (x << k) & 0xff;
    if (x & 1) {
        v |= (uint8_t)((1u << k) - 1u);
    }
    return v;
}

// One state-code transition step, see file header.
static uint8_t elsner_step(uint8_t prev, uint8_t base, int old, int new_)
{
    return (uint8_t)(elsner_spread(prev, 1) + base + new_ - old);
}

// Self-synchronizing transform, G(x) = x^7 + x^5 + 1:
// plain[n] = onair[n] ^ onair[n-5] ^ onair[n-7], MSB first, in[<0] = 0.
static void elsner_descramble(uint8_t const *in, uint8_t *out, int nbytes)
{
    int nbits = nbytes * 8;
    for (int i = 0; i < nbytes; i++) {
        out[i] = 0;
    }
    for (int n = 0; n < nbits; n++) {
        int bit = (in[n / 8] >> (7 - (n % 8))) & 1;
        if (n - 5 >= 0) {
            bit ^= (in[(n - 5) / 8] >> (7 - ((n - 5) % 8))) & 1;
        }
        if (n - 7 >= 0) {
            bit ^= (in[(n - 7) / 8] >> (7 - ((n - 7) % 8))) & 1;
        }
        out[n / 8] |= bit << (7 - (n % 8));
    }
}

// q[n] = onair[n] ^ onair[n-7], MSB first, in[<0] = 0; used only to locate
// the family/branch selector bits, see file header.
static void elsner_lag7(uint8_t const *in, uint8_t *out, int nbytes)
{
    int nbits = nbytes * 8;
    for (int i = 0; i < nbytes; i++) {
        out[i] = 0;
    }
    for (int n = 0; n < nbits; n++) {
        int bit = (in[n / 8] >> (7 - (n % 8))) & 1;
        if (n - 7 >= 0) {
            bit ^= (in[(n - 7) / 8] >> (7 - ((n - 7) % 8))) & 1;
        }
        out[n / 8] |= bit << (7 - (n % 8));
    }
}

// Map a state-normalized command token to a descriptive label, see file
// header. Returns NULL for an unrecognized token.
static char const *elsner_command_name(uint16_t token)
{
    switch (token) {
    case 0xcc00: return "close";
    case 0xbb00: return "open";
    case 0xbd00: return "stop_or_release";
    case 0x00ef: return "automode_a";
    case 0x00e3: return "automode_b";
    case 0xaac0: return "filler";
    case 0xa9c0: return "automode_companion";
    default: return NULL;
    }
}

static int elsner_solexa_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = 0; // we expect a single row only
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t const anchor[] = {0x0a};
    unsigned pos = bitbuffer_search(bitbuffer, row, 0, anchor, 8) + 8;

    unsigned len = bitbuffer->bits_per_row[row];
    if (pos >= len || len - pos < 38 * 8) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[38];
    bitbuffer_extract_bytes(bitbuffer, row, pos, b, sizeof(b) * 8);

    // CRC-16 is over the on-air (scrambled) bytes; this is the integrity gate.
    uint16_t crc_calc = crc16(b, 36, 0x1021, 0x68b3);
    uint16_t crc_msg  = (b[36] << 8) | b[37];
    if (crc_calc != crc_msg) {
        return DECODE_FAIL_MIC;
    }

    // Remove the fixed transform for the constant sync/id marker (0xc945a400)
    // and an opaque diagnostic payload. The id is reported, not gated on,
    // since it may be a per-unit value rather than a protocol constant.
    uint8_t p[36];
    elsner_descramble(b, p, 36);

    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x%02x", p[0], p[1], p[2], p[3]);

    char payload_str[65];
    for (int i = 0; i < 32; i++) {
        snprintf(&payload_str[i * 2], 3, "%02x", p[4 + i]);
    }

    // Parse and validate the raw on-air p0..p10 rolling-state model (see
    // file header). Raw payload byte p_k is b[4 + k]. p3/p4/p5 are fixed
    // universal functions of the rolling state p0, so they validate the
    // whole expansion.
    uint8_t rp0    = b[4];
    int param_ok   = b[7] == ((elsner_spread(rp0, 3) + 0x48) & 0xff)
            && b[8] == ((elsner_spread(rp0, 4) + 0xd0) & 0xff)
            && b[9] == ((elsner_spread(rp0, 5) + 0xa0) & 0xff);
    int counter = (b[5] - elsner_spread(rp0, 1)) & 0xff;
    if (!param_ok) {
        decoder_log(decoder, 1, __func__, "p0-p10 rolling-state model did not validate");
    }

    char rolling_str[3];
    snprintf(rolling_str, sizeof(rolling_str), "%02x", rp0);

    // Locate the family/branch selector bits and, for the one confirmed
    // family, the state-normalized command token (see file header).
    uint8_t q[36];
    elsner_lag7(b, q, 36);
    int family1 = (q[4 + 3] >> 4) & 1;
    char const *command = NULL;
    if (param_ok && family1) {
        int branch1 = (q[4 + 17] >> 2) & 1;
        int l12 = parity8(rp0 & 0xf9);
        int l13 = parity8(rp0 & 0xfd);
        int l14 = 1 ^ parity8(rp0 & 0xff);
        int l17 = parity8(rp0 & 0xfe);
        int l18 = 1 ^ parity8(rp0 & 0xff);
        int l19 = parity8(rp0 & 0xfe);

        uint8_t exp0, exp1, obs0, obs1;
        if (!branch1) {
            exp0 = elsner_step(b[4 + 12], 0x6a, l12, l13);
            exp1 = elsner_step(exp0, 0x30, l13, l14);
            obs0 = b[4 + 13];
            obs1 = b[4 + 14];
        }
        else {
            exp0 = elsner_step(b[4 + 17], 0x30, l17, l18);
            exp1 = elsner_step(exp0, 0x30, l18, l19);
            obs0 = b[4 + 18];
            obs1 = b[4 + 19];
        }
        uint16_t token = (uint16_t)(((obs0 - exp0) & 0xff) << 8) | ((obs1 - exp1) & 0xff);
        command        = elsner_command_name(token);
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",             DATA_STRING, "Elsner-Solexa",
            "id",       "Sync/ID",      DATA_STRING, id_str,
            "rolling",  "Rolling state", DATA_COND, param_ok, DATA_STRING, rolling_str,
            "counter",  "Counter",      DATA_COND, param_ok, DATA_INT, counter,
            "command",  "Command",      DATA_COND, command != NULL, DATA_STRING, command ? command : "",
            "data",     "Data",         DATA_STRING, payload_str,
            "mic",      "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "rolling",
        "counter",
        "command",
        "data",
        "mic",
        NULL,
};

r_device const elsner_solexa = {
        .name        = "Elsner Solexa 230V",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 11,
        .long_width  = 11,
        .reset_limit = 25,
        .decode_fn   = &elsner_solexa_decode,
        .fields      = output_fields,
};
