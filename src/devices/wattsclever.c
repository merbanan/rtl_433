/** @file
    Watts Clever EW4500 (Hideki WAC22) optical energy meter reader decoder.

    Copyright (C) 2026 Shaun Voigt <26222217+smvoigt@users.noreply.github.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

#include "decoder.h"
#include <stdlib.h>

#define WATTSCLEVER_SYNC_BYTE  0xFA
#define WATTSCLEVER_SHORT_BITS 56
#define WATTSCLEVER_LONG_BITS  72

#define WC_MAX_CANDS  64
#define WC_MAX_TRACKS 64

// Universal nibble whitening tables (indices 0..22 and 0..21 cover off=0..15, i=0..6)
static uint8_t const wc_table_a[23] = {
        0x1, 0x8, 0xd, 0x3, 0xb, 0xc, 0x4, 0x9, 0xc, 0x9, 0x5, 0x2,
        0x1, 0x3, 0xd, 0x0, 0x9, 0xc, 0xe, 0xc, 0x7, 0xe, 0x6,
};

static uint8_t const wc_table_y[22] = {
        0x6, 0x4, 0x2, 0x3, 0x9, 0x9, 0x8, 0xd, 0x2, 0xb, 0xb, 0xe,
        0x9, 0x4, 0x5, 0xc, 0x3, 0xd, 0x1, 0x6, 0x7, 0x8,
};

typedef struct {
    uint16_t id_bcd;
    uint8_t channel;
    uint8_t msub;
    uint8_t offc;
    uint8_t p2_lo;
    uint8_t low_battery;
    uint32_t gap;
    uint64_t impulses;
} wc_cand_t;

typedef struct {
    uint8_t in_use;
    uint16_t id_bcd;
    uint8_t channel;
    uint8_t msub;
    uint16_t offc_mask;
    uint16_t seen_56;
    uint16_t seen_72;
    uint32_t rk56_bits[8];
    uint32_t rk72_bits[8];
    uint32_t last_seen;
    uint8_t last_was_56_seq1;
    uint32_t last_56_seq1_gap;
} wc_track_t;

typedef struct {
    uint32_t seq;
    wc_track_t tracks[WC_MAX_TRACKS];
    uint8_t pending_valid;
    uint8_t pending_is_long;
    int pending_ncands;
    wc_cand_t pending_cands[WC_MAX_CANDS];
} wc_state_t;

static inline int wc_is_bcd16(uint16_t x)
{
    return (x & 0x000F) <= 9 &&
           ((x >> 4) & 0x000F) <= 9 &&
           ((x >> 8) & 0x000F) <= 9 &&
           ((x >> 12) & 0x000F) <= 9;
}

static inline int wc_bcd16_to_int(uint16_t x)
{
    return (int)(((x >> 12) & 0x000F) * 1000 +
                 ((x >> 8) & 0x000F) * 100 +
                 ((x >> 4) & 0x000F) * 10 +
                 (x & 0x000F));
}

static inline int wc_offc_fits_pair(uint16_t mask16)
{
    for (int c = 0; c < 16; ++c) {
        uint16_t allowed = (uint16_t)((1u << c) | (1u << ((c + 1) & 0x0F)));
        if ((mask16 & ~allowed) == 0) {
            return 1;
        }
    }
    return 0;
}

static inline int wc_track_score(wc_track_t const *t)
{
    int both = (t->seen_56 > 0 && t->seen_72 > 0) ? 1 : 0;
    return both * 1000 + (int)(t->seen_56 + t->seen_72) * 10;
}

static int wc_find_candidates(uint8_t const *rv, int plen, wc_cand_t *cands, int max_cands)
{
    int count     = 0;
    uint8_t rk    = rv[plen];
    uint8_t a     = rk >> 4;
    uint8_t b_lo  = rk & 0x0F;
    uint16_t w16  = (uint16_t)(((rv[2] & 0x0F) << 12) | (rv[1] << 4) | (rv[0] >> 4));
    int want_long = (plen == 7) ? 1 : 0;

    for (int off = 0; off < 16; ++off) {
        for (int mprev = 0; mprev < 16; ++mprev) {
            uint8_t s = (uint8_t)((mprev - wc_table_a[off]) & 0x0F);
            uint8_t mask[7];
            uint8_t prev_hi = (uint8_t)mprev;
            for (int i = 0; i < plen; ++i) {
                uint8_t lo = (uint8_t)(prev_hi ^ wc_table_y[off + i]);
                if (i == 0) {
                    lo &= 0x0E; // bit 0 is the unscrambled frame-length bit
                }
                uint8_t hi = (uint8_t)((wc_table_a[off + 1 + i] + s) & 0x0F);
                mask[i]    = (uint8_t)((hi << 4) | lo);
                prev_hi    = hi;
            }

            uint8_t p0_lo = (uint8_t)((rv[0] ^ mask[0]) & 0x0F);
            if ((p0_lo & 0x01) != want_long) {
                continue;
            }
            int channel = ((p0_lo & 0x0E) >> 1) + 1;
            if (channel < 1 || channel > 5) {
                continue;
            }

            uint8_t p2_hi = (uint8_t)((rv[2] ^ mask[2]) >> 4);
            uint8_t p3    = (uint8_t)(rv[3] ^ mask[3]);
            uint8_t p4    = (uint8_t)(rv[4] ^ mask[4]);
            uint8_t p5    = want_long ? (uint8_t)(rv[5] ^ mask[5]) : 0;
            uint8_t p6    = want_long ? (uint8_t)(rv[6] ^ mask[6]) : 0;

            if (!want_long && (p4 >> 4) != 0) {
                continue;
            }
            if (want_long && (p6 & 0x80) != 0) {
                continue;
            }

            int max_seq  = want_long ? 1 : 2;
            uint8_t msub = (uint8_t)((b_lo - mprev) & 0x0F);
            uint8_t offc = (uint8_t)((off - (a - b_lo)) & 0x0F);

            for (int p2_lo = 1; p2_lo <= max_seq; ++p2_lo) {
                uint8_t p1_lo = (uint8_t)((b_lo ^ p0_lo ^ p2_lo ^ (p3 & 0x0F) ^ (p4 & 0x0F) ^ (p5 & 0x0F) ^ (p6 & 0x0F)) & 0x0F);
                if (p1_lo > 9) {
                    continue;
                }
                for (int p1_hi = 0; p1_hi <= 9; ++p1_hi) {
                    uint8_t p1      = (uint8_t)((p1_hi << 4) | p1_lo);
                    uint8_t p0_hi   = (uint8_t)((a ^ p1_hi ^ p2_hi ^ (p3 >> 4) ^ (p4 >> 4) ^ (p5 >> 4) ^ (p6 >> 4)) & 0x0F);
                    uint16_t u16    = (uint16_t)((((p2_lo ^ (mask[2] & 0x0F)) & 0x0F) << 12) |
                                                 (((p1 ^ mask[1]) & 0xFF) << 4) |
                                                 ((p0_hi ^ (mask[0] >> 4)) & 0x0F));
                    uint16_t id_bcd = (uint16_t)(w16 - u16);

                    if (((id_bcd >> 4) & 0xFF) == p1 && wc_is_bcd16(id_bcd)) {
                        if (count < max_cands) {
                            wc_cand_t *c   = &cands[count++];
                            c->id_bcd      = id_bcd;
                            c->channel     = (uint8_t)channel;
                            c->msub        = msub;
                            c->offc        = offc;
                            c->p2_lo       = (uint8_t)p2_lo;
                            c->low_battery = want_long ? (uint8_t)((p6 >> 6) & 0x01) : 0;
                            c->gap         = !want_long ? ((uint32_t)p2_hi | ((uint32_t)p3 << 4) | ((uint32_t)(p4 & 0x0F) << 12)) : 0;
                            c->impulses    = want_long ? ((uint64_t)p2_hi |
                                                          ((uint64_t)p3 << 4) |
                                                          ((uint64_t)p4 << 12) |
                                                          ((uint64_t)p5 << 20) |
                                                          (((uint64_t)p6 & 0x3F) << 28))
                                                       : 0;
                        }
                    }
                }
            }
        }
    }

    return count;
}

static wc_track_t *wc_find_or_alloc_track(wc_state_t *st, uint16_t id_bcd, uint8_t channel, uint8_t msub)
{
    int free_idx = -1;
    for (int i = 0; i < WC_MAX_TRACKS; ++i) {
        if (st->tracks[i].in_use) {
            if (st->tracks[i].id_bcd == id_bcd &&
                    st->tracks[i].channel == channel &&
                    st->tracks[i].msub == msub) {
                return &st->tracks[i];
            }
        }
        else if (free_idx < 0) {
            free_idx = i;
        }
    }

    if (free_idx < 0) {
        // Evict the lowest-scoring, least-recently-seen track
        int victim       = 0;
        int victim_score = wc_track_score(&st->tracks[0]);
        for (int i = 1; i < WC_MAX_TRACKS; ++i) {
            int sc = wc_track_score(&st->tracks[i]);
            if (sc < victim_score ||
                    (sc == victim_score && st->tracks[i].last_seen < st->tracks[victim].last_seen)) {
                victim       = i;
                victim_score = sc;
            }
        }
        free_idx = victim;
    }

    wc_track_t *t = &st->tracks[free_idx];
    memset(t, 0, sizeof(*t));
    t->in_use  = 1;
    t->id_bcd  = id_bcd;
    t->channel = channel;
    t->msub    = msub;
    return t;
}

/**
Watts Clever EW4500 (Hideki WAC22) optical energy meter reader decoder.

Direct protocol sibling of the Norgo NGE101 (norgo.c), adding a 16-bit BCD
transmitter ID and a nibble-stream whitening layer keyed by the XOR checksum
byte. Tested on Watts Clever EW4500 (Hideki WAC22 PCB) hardware across 21
transmitter IDs, channels 1-5, 0 W to 69.55 kW, and supply voltages from
2.43 V to 3.01 V.

Signal and framing:
- Frequency: 433.92 MHz (nominal; measured ~433.85 MHz)
- Modulation: inverted OOK Differential Manchester Coding (OOK_PULSE_DMC),
  clocked from a 32.768 kHz crystal (short_width = 488 us, long_width = 976 us).
  After bitbuffer_invert(), no mid-bit transition = 1, mid-bit transition = 0.
- Cadence: alternating 2-frame bursts every (31 - 2 * channel) seconds
  (29 s on CH 1, 21 s on CH 5). Burst A sends a 72-bit frame followed by a
  56-bit frame (~178 ms apart); Burst B sends two 56-bit frames (~163 ms apart).
- Each frame begins with the 8-bit sync byte 0xFA followed by 56 bits (7 bytes)
  or 72 bits (9 bytes). A trailing 1 bit sent as carrier-OFF merges into the
  end-of-frame silence (bits == 63 or 79) and is restored before CRC checking.
- Outer integrity: CRC-8 (poly = 0x07, init = 0x00) over the 7 or 9 raw bytes
  following 0xFA.

De-whitening (after reflect_bytes() to LSB-first byte order):
- Let plen = nbytes - 2 (5 for 56-bit, 7 for 72-bit) and rK = rv[plen].
- Universal nibble whitening tables wc_table_a[23] and wc_table_y[22] generate
  a byte mask M[0..plen-1] from (off, mprev) in 0..15 x 0..15, where
  S = (mprev - A[off]) & 0x0F, M[i]_lo = prev_hi ^ Y[off + i] (with bit 0 of
  M[0] cleared), and M[i]_hi = (A[off + 1 + i] + S) & 0x0F.
- For any fixed transmitter ID, mprev = (rK_lo - msub) & 0x0F (constant msub)
  and off = (rK_hi - rK_lo + offc) & 0x0F where offc takes at most two adjacent
  cyclic values {c, (c + 1) & 0x0F}.
- A 16-bit BCD transmitter ID (e.g. 0x1063 for LCD ID 1063) is added mod 2^16
  to the nibble-aligned window U16 = (u[2]_lo << 12) | (u[1] << 4) | u[0]_hi
  after masking.
- Plaintext bytes P[0..plen-1] satisfy the byte-wise XOR checksum
  P[0] ^ P[1] ^ ... ^ P[plen - 1] == rK.
- Because (msub, offc) varies nonlinearly per ID, the decoder evaluates the 256
  (off, mprev) states against the XOR, BCD ID, P[1] == (ID_BCD >> 4) & 0xFF,
  channel, and frame-type constraints, and locks onto each transmitter within
  its first 2-frame burst (~178 ms) via decoder->decode_ctx.

Plaintext data layout (LSB-first bytes P[0..plen-1]):

56-bit frame (plen = 5, instantaneous impulse gap):

    P[0]     P[1]     P[2]     P[3]     P[4]     rv[5]    rv[6]
    NNNNCCCF DDDDDDDD GGGGSSSS GGGGGGGG 0000GGGG XXXXXXXX CCCCCCCC

72-bit frame (plen = 7, cumulative impulses and battery):

    P[0]     P[1]     P[2]     P[3]     P[4]     P[5]     P[6]     rv[7]    rv[8]
    NNNNCCCF DDDDDDDD KKKKSSSS KKKKKKKK KKKKKKKK KKKKKKKK 0BKKKKKK XXXXXXXX CCCCCCCC

- N: 4-bit rolling nonce (copies previous frame's rv[0] >> 4)
- C: 3-bit channel - 1 (channels 1..5 -> 0..4)
- F: 1-bit frame type (0 = 56-bit gap, 1 = 72-bit impulse counter)
- D: middle two BCD digits of the 4-digit ID, (ID_BCD >> 4) & 0xFF
- S: 4-bit packet sequence within burst (1 = first frame, 2 = second frame)
- G: 16-bit little-endian impulse gap in 1/1024 s ticks (0 = no load / 0 W)
- K: 34-bit little-endian cumulative impulse count (reported as uint32_t)
- B: 1-bit low battery flag (1 at <= 2.46 V, 0 at >= 2.48 V)
- X: 8-bit XOR checksum of P[0..plen-1]
- C (last byte): 8-bit CRC-8 (poly 0x07, init 0x00)

Converting native output fields to kW and kWh:
- The sensor reports raw optical impulses and inter-impulse time gaps because
  meter pulse rates (imp_per_kwh, printed on the electricity meter faceplate)
  vary by meter (commonly 1000, 800, 1600, or 3200 imp/kWh).
- Instantaneous power from `gap` (in 1/1024 s ticks):
  - Time between impulses: T_s = gap / 1024.0
  - power_kW = 3686400.0 / (imp_per_kwh * gap)   (0 when gap == 0)
  - power_W  = power_kW * 1000.0
  - Example for a 1000 imp/kWh meter:
    - power_kW = 3686.4 / gap   (power_W = 3686400.0 / gap)
    - If gap = 3686: power_kW = 3686.4 / 3686 = 1.000 kW (1000 W)
    - If gap = 1843: power_kW = 3686.4 / 1843 = 2.000 kW (2000 W)
- Cumulative energy from `impulses`:
  - energy_kWh = impulses / imp_per_kwh
  - Example for a 1000 imp/kWh meter:
    - energy_kWh = impulses / 1000.0
    - If impulses = 52602468: energy_kWh = 52602.468 kWh
*/
static int wattsclever_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int events = 0;
    int ret    = DECODE_ABORT_LENGTH;

    if (!decoder->decode_ctx) {
        decoder->decode_ctx = calloc(1, sizeof(wc_state_t));
        if (!decoder->decode_ctx) {
            return DECODE_ABORT_EARLY;
        }
    }
    wc_state_t *st = (wc_state_t *)decoder->decode_ctx;

    bitbuffer_invert(bitbuffer);
    decoder_log_bitbuffer(decoder, 2, __func__, bitbuffer, "");

    for (int r = 0; r < bitbuffer->num_rows; ++r) {
        int bits = bitbuffer->bits_per_row[r];
        // Expect 8-bit sync (0xFA) + 56-bit or 72-bit frame (allow -1..+2 trailing bits from DMC)
        int payload_bits = 0;
        if (bits >= 8 + WATTSCLEVER_LONG_BITS - 1 && bits <= 8 + WATTSCLEVER_LONG_BITS + 2) {
            payload_bits = WATTSCLEVER_LONG_BITS;
        }
        else if (bits >= 8 + WATTSCLEVER_SHORT_BITS - 1 && bits <= 8 + WATTSCLEVER_SHORT_BITS + 2) {
            payload_bits = WATTSCLEVER_SHORT_BITS;
        }
        else {
            continue;
        }

        uint8_t *row = bitbuffer->bb[r];
        if (row[0] != WATTSCLEVER_SYNC_BYTE) {
            decoder_logf(decoder, 2, __func__, "Sync mismatch: 0x%02x (bits=%d)", row[0], bits);
            ret = DECODE_ABORT_EARLY;
            continue;
        }

        int nbytes = payload_bits / 8;
        uint8_t b[9];
        memcpy(b, row + 1, nbytes);

        // In inverted OOK_PULSE_DMC, a final '1' bit sent as carrier-OFF merges into the
        // end-of-frame silence, leaving bits == 63 (or 79). Restore that implicit trailing '1'.
        if (bits == 8 + payload_bits - 1) {
            b[nbytes - 1] |= 0x01;
        }

        // Verify CRC-8 (poly = 0x07, init = 0x00)
        if (crc8(b, nbytes, 0x07, 0x00) != 0) {
            decoder_logf_bitrow(decoder, 1, __func__, b, payload_bits, "CRC-8 mismatch");
            ret = DECODE_FAIL_MIC;
            continue;
        }

        // Reflect on-wire LSB-first bytes
        uint8_t rv[9];
        memcpy(rv, b, nbytes);
        reflect_bytes(rv, nbytes);

        int plen    = nbytes - 2; // 5 for 56-bit, 7 for 72-bit
        uint8_t rk  = rv[plen];
        int is_long = (plen == 7) ? 1 : 0;

        wc_cand_t cands[WC_MAX_CANDS];
        int ncands = wc_find_candidates(rv, plen, cands, WC_MAX_CANDS);
        if (ncands == 0) {
            decoder_logf_bitrow(decoder, 2, __func__, b, payload_bits, "No whitening match (likely idle Packet B)");
            ret = DECODE_FAIL_OTHER;
            continue;
        }

        st->seq++;

        // Update tracks for each distinct (id_bcd, channel, msub) in this frame
        for (int i = 0; i < ncands; ++i) {
            int dup = 0;
            for (int j = 0; j < i; ++j) {
                if (cands[j].id_bcd == cands[i].id_bcd &&
                        cands[j].channel == cands[i].channel &&
                        cands[j].msub == cands[i].msub) {
                    dup = 1;
                    break;
                }
            }
            if (dup) {
                continue;
            }

            wc_track_t *t = wc_find_or_alloc_track(st, cands[i].id_bcd, cands[i].channel, cands[i].msub);
            t->last_seen  = st->seq;

            // Update offc_mask if any matching candidate keeps offc within a 2-value cyclic pair
            int offc_ok = 0;
            for (int j = i; j < ncands; ++j) {
                if (cands[j].id_bcd == cands[i].id_bcd &&
                        cands[j].channel == cands[i].channel &&
                        cands[j].msub == cands[i].msub) {
                    uint16_t nm = (uint16_t)(t->offc_mask | (1u << cands[j].offc));
                    if (wc_offc_fits_pair(nm)) {
                        t->offc_mask = nm;
                        offc_ok      = 1;
                        break;
                    }
                }
            }

            if (offc_ok) {
                uint32_t *rk_bits = is_long ? t->rk72_bits : t->rk56_bits;
                uint32_t bitmask  = 1u << (rk & 31);
                if ((rk_bits[rk >> 5] & bitmask) == 0) {
                    rk_bits[rk >> 5] |= bitmask;
                    if (is_long) {
                        t->seen_72++;
                    }
                    else {
                        t->seen_56++;
                    }
                }
            }
        }

        // Select the highest-scoring consistent candidate(s)
        int best_score       = -1;
        int best_idx         = -1;
        int payload_conflict = 0;
        for (int i = 0; i < ncands; ++i) {
            wc_track_t *t = wc_find_or_alloc_track(st, cands[i].id_bcd, cands[i].channel, cands[i].msub);
            if (!wc_offc_fits_pair((uint16_t)(t->offc_mask | (1u << cands[i].offc)))) {
                continue;
            }
            int sc = wc_track_score(t);
            if (sc > best_score) {
                best_score       = sc;
                best_idx         = i;
                payload_conflict = 0;
            }
            else if (sc == best_score && best_idx >= 0) {
                wc_cand_t const *b0 = &cands[best_idx];
                wc_cand_t const *bi = &cands[i];
                if (b0->id_bcd != bi->id_bcd || b0->channel != bi->channel ||
                        (!is_long && b0->gap != bi->gap) ||
                        (is_long && (b0->impulses != bi->impulses || b0->low_battery != bi->low_battery))) {
                    payload_conflict = 1;
                }
            }
        }

        // Require lock (>= 2 distinct frames with consistent ID/channel/msub/offc) before emitting.
        // Buffer the initial frame so that once the second frame of the first burst (~178 ms later)
        // locks the track, both frames (e.g. 72-bit impulses + 56-bit gap) are emitted cleanly.
        if (best_idx < 0 || best_score < 20) {
            st->pending_valid   = 1;
            st->pending_is_long = (uint8_t)is_long;
            st->pending_ncands  = ncands;
            memcpy(st->pending_cands, cands, (size_t)ncands * sizeof(wc_cand_t));
            decoder_logf_bitrow(decoder, 2, __func__, b, payload_bits, "Buffering initial frame until 2-frame lock (score=%d)", best_score);
            ret = DECODE_FAIL_OTHER;
            continue;
        }
        if (payload_conflict) {
            decoder_logf_bitrow(decoder, 2, __func__, b, payload_bits, "Ambiguous payload across tied tracks (score=%d)", best_score);
            ret = DECODE_FAIL_OTHER;
            continue;
        }

        wc_cand_t const *sel = &cands[best_idx];
        wc_track_t *locked_t = wc_find_or_alloc_track(st, sel->id_bcd, sel->channel, sel->msub);

        // Queue buffered first frame (if it matches this newly locked track) plus current frame
        wc_cand_t const *emit_cands[2];
        int emit_long[2];
        int n_emit = 0;

        if (st->pending_valid) {
            for (int i = 0; i < st->pending_ncands; ++i) {
                wc_cand_t const *pc = &st->pending_cands[i];
                if (pc->id_bcd == sel->id_bcd &&
                        pc->channel == sel->channel &&
                        pc->msub == sel->msub &&
                        wc_offc_fits_pair((uint16_t)(locked_t->offc_mask | (1u << pc->offc)))) {
                    emit_cands[n_emit] = pc;
                    emit_long[n_emit]  = (int)st->pending_is_long;
                    n_emit++;
                    break;
                }
            }
            st->pending_valid = 0;
        }

        emit_cands[n_emit] = sel;
        emit_long[n_emit]  = is_long;
        n_emit++;

        decoder_logf_bitrow(decoder, 2, __func__, b, payload_bits, "Decoded id=%04d ch=%d seq=%d", wc_bcd16_to_int(sel->id_bcd), sel->channel, sel->p2_lo);

        for (int e = 0; e < n_emit; ++e) {
            wc_cand_t const *ec = emit_cands[e];
            int device_id       = wc_bcd16_to_int(ec->id_bcd);

            if (!emit_long[e]) {
                // Burst B sends two 56-bit frames (seq=1 then seq=2, ~163 ms apart) with identical gap.
                // Suppress the seq=2 duplicate if seq=1 was just emitted with the same gap;
                // if seq=1 was missed or this is Burst A (72b -> 56b seq=2), seq=2 is still emitted.
                if (ec->p2_lo == 2 && locked_t->last_was_56_seq1 && ec->gap == locked_t->last_56_seq1_gap) {
                    locked_t->last_was_56_seq1 = 0;
                    continue;
                }
                if (ec->p2_lo == 1) {
                    locked_t->last_was_56_seq1 = 1;
                    locked_t->last_56_seq1_gap = ec->gap;
                }
                else {
                    locked_t->last_was_56_seq1 = 0;
                }

                /* clang-format off */
                data_t *data = data_make(
                        "model",      "",             DATA_STRING, "WattsClever-EW4500",
                        "id",         "Device ID",    DATA_INT,    device_id,
                        "channel",    "Channel",      DATA_INT,    (int)ec->channel,
                        "gap",        "Impulse gap",  DATA_INT,    (int)ec->gap,
                        "mic",        "Integrity",    DATA_STRING, "CRC",
                        NULL);
                /* clang-format on */
                decoder_output_data(decoder, data);
            }
            else {
                locked_t->last_was_56_seq1 = 0;
                // Pulse count is 34 bits; report lower 32 bits (same convention as norgo.c)
                /* clang-format off */
                data_t *data = data_make(
                        "model",      "",             DATA_STRING, "WattsClever-EW4500",
                        "id",         "Device ID",    DATA_INT,    device_id,
                        "channel",    "Channel",      DATA_INT,    (int)ec->channel,
                        "impulses",   "Impulses",     DATA_INT,    (uint32_t)ec->impulses,
                        "battery_ok", "Battery",      DATA_INT,    !ec->low_battery,
                        "mic",        "Integrity",    DATA_STRING, "CRC",
                        NULL);
                /* clang-format on */
                decoder_output_data(decoder, data);
            }
            events++;
        }
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "gap",
        "impulses",
        "battery_ok",
        "mic",
        NULL,
};

r_device const wattsclever = {
        .name        = "Watts Clever EW4500 (Hideki WAC22) Optical Energy Meter",
        .modulation  = OOK_PULSE_DMC,
        .short_width = 488,  // half-bit width (32.768 kHz / 64 = 488.28 us)
        .long_width  = 976,  // full-bit width (32.768 kHz / 32 = 976.56 us)
        .reset_limit = 2500, // inter-frame gap is ~90 ms; max intra-frame gap ~1015 us
        .tolerance   = 200,  // absorbs ~30 us OOK pulse/gap slicer bias
        .decode_fn   = &wattsclever_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
