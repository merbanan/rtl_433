/** @file
    Silver Spring Networks mesh network endpoint (narrow-band FHSS PHY).

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"
#include <stdarg.h>

/** @fn static int silver_spring_mesh_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Silver Spring Networks mesh network endpoint -- narrow-band FHSS PHY.

- Issue #3583 (reported against a Badger Meter GIF2014W-OSE water meter,
  FCC ID GIF2014W-OSE, City of Broomfield, CO): these frames are AMI mesh
  traffic that the endpoint rides on, not Badger's own fixed-network
  protocol (see decoder 282/290 `orion_endpoint` for that).

This PHY/MAC is Silver Spring Networks' narrow-band FHSS mesh (Itron
acquired Silver Spring Networks in 2018), based on the IEEE 802.15.4g draft
per the Atmel/ADI/Silver Spring contribution mentor.ieee.org 15-09-0298 and
patent US20090303972A1 (assigned to Silver Spring Networks): 100 kbit/s
2-FSK, no FEC, an 8-bit payload scrambler seeded per hop, and an IEEE CRC-32
FCS over the *descrambled* payload. It is emitted by a mobile (drive-by)
mesh reader that logs whole neighbourhoods of endpoints in one pass, so one
frame usually lists several devices.

On air, byte-aligned, no data whitening on the header and no trailing pad bit:

    preamble(0xAA..) | SFD 0xF3A0 | PHR(3) | scrambled PSDU(N) | scrambled FCS(4)

Both FSK polarities occur in the wild, so the sync and its bit-complement are
both searched.

PHR (3 bytes, in the clear): `seed(8) | FCTRL(4) | EXT(1) | length(11)`.

- seed: {8} RF channel / hop index. Reported as `channel`.
- FCTRL: {4} always `0000` in every frame seen (no-FEC/default-100kbps);
  plausibility gate.
- EXT: {1} always `0` in every frame seen; plausibility gate.
- length: {11} PSDU length in octets, `((byte1 & 0x07) << 8) | byte2`.
  Frame after the SFD is `3 + length + 4` bytes.

PSDU + FCS are scrambled by an 8-bit additive LFSR (`x^8+x^4+x^3+x^2+1`,
tap 0x8e) seeded with a per-hop value. The FCS is a CRC-32/MPEG-2 (poly
0x04c11db7, init 0xffffffff, no final XOR, MSB-first) over the *descrambled*
PSDU. The per-hop seed is an 8-bit value not simply derived from `CC`; it is
recovered here by trying all 255 nonzero seeds and keeping the one whose
descrambled PSDU matches the trailing CRC-32. A random frame passes for at
most a handful of seeds by chance (~255/2^32), so a CRC match is a genuine
integrity check -- the decoder reports `mic: CRC` and is enabled by default.

Descrambled PSDU (application layer, the Silver Spring MAC format of patent
US20090310511A1): `FCTRL | [destination EUI-64] | [source EUI-64] | TLVs`.

- FCTRL: {8} bit 0 selects a destination address, bit 1 a source address.
  Observed values are `0x01` (destination only), `0x02` (source only), and
  `0x03` (destination then source) -- reported as `dst_id` / `src_id`.
- Each **IEEE EUI-64 address** carries one of two OUIs, both confirmed
  against the IEEE OUI registry (standards-oui.ieee.org/oui/oui.csv):
  `00:13:50` (Silver Spring Networks, Inc.; the trailing `ff fe xx xx xx` is
  the standard IEEE EUI-48 -> EUI-64 expansion) or `00:07:81` (Itron Inc.'s
  own MA-L, used post-acquisition alongside the legacy Silver Spring OUI).
- What follows the address(es) is a TLV stream of DLL and MPDU type/length/
  value records, carrying link-layer control, routing, and application data.
  Every TLV has a 2-byte header whose first byte's high bit selects the
  layout: DLL (`type = (h0<<1)|(h1>>7)`, a 9-bit type + 7-bit length) or MPDU
  (`type = h0>>3`, a 5-bit type + 11-bit length). Reported as `tlvs`, a
  space-separated `D<type>/<len>` or `M<type>/<len>` list (MPDU type 17
  always carries exactly one nested TLV, shown as `M17/<len>{...}`). DLL
  type 6 is a mid-stream marker (not the frame's last record) meaning the
  4 bytes at the very end of the PSDU are a CRC-32/MPEG-2 over everything
  before them; once the rest of the stream is walked, that trailing check
  is appended as `:ok`/`:bad`. DLL type 5 does end the stream immediately,
  before a fragment's raw payload, which this decoder does not reassemble
  across frames -- shown as a trailing `+NB` in `tlvs`, with the full bytes
  always available in `data`.

Established/best-evidence TLV values are decoded further:

- **Link layer** (MAC patent's unicast poll/poll-ack/data/data-ack procedure,
  plus FCTRL 2 broadcasts): reported as `link`, classified from FCTRL and
  which TLV types are present. DLL type 4, Sequence Control, is a 16-bit
  `retry(1)|more-fragments(1)|fragment-number(4)|sequence-number(10)` field,
  reported as `retry`/`frag_more`/`frag_num`/`seq_num`. DLL type 1, the
  Fractional Epoch Tick, is a 16-bit value (a fraction through a hop epoch
  scaled by 65536, per the hopping patent) reported raw as `fet`. DLL type 9
  is a signed-byte RSSI candidate (observed range -103..-73 dBm-like, exact
  calibration unconfirmed), reported as `rssi`. DLL type 2, the Communication
  Link Info candidate, is a 24-bit `tx_priority(3)|tx_time(9)|rx_priority(3)|
  rx_time(9)` packing, reported as `cli_tx_pri`/`cli_tx_time`/`cli_rx_pri`/
  `cli_rx_time`. DLL type 8, a channel-synchronization candidate, has an
  established final byte reported as `sync_channel`; its preceding flag bytes
  are not yet decoded.
- **Network/routing** (MPDU type 16, the network/routing patents' byte-local
  revision of the common network header, US20080051036A1): either a direct
  sequence of route advertisement objects (each `version/type(1, always
  0x21) | max-hop candidate(1) | prefix/control candidate(2, always 0x1000)
  | network address(16, zero) | egress EUI-64(8) | descriptor(next-hop
  count(1) | route-flag candidates(3) | path-cost candidate(2) | link-cost
  candidate(2)) | next-hop EUI-64 * count`), or that same object stream
  wrapped in a header (`version(4)|protocol_id(4), control(2)|TTL(6),
  control(2)|route-offset(6), priority(1)|source-route(1)|address-count(6)`,
  then that many EUI-64s) whose protocol id selects a route-object payload
  (3) or an IPv6 datagram (6). Route objects are reported as `routes`, one
  `hop<=H cost=P/L egress=<eui64>[ next=<eui64> ...]` per object.
- **IPv6/UDP**: reported as `ipv6` (`[src] -> [dst] sport->dport len=N
  cksum=ok/bad`), with the UDP checksum verified against the IPv6
  pseudo-header.
- **Management/security**: UDP destination port 648 is the AMI FAN
  traffic-analysis patent's secure Management command port; its declared
  cleartext-length field (the observed `12-byte envelope + declared length +
  28 bytes of crypto overhead` framing) is appended to `ipv6` as `mgmt_len`.
  The cipher suite, IV/nonce split, authentication construction, and the
  encrypted contents themselves remain unknown and are not touched.

Naming several individual field values still rests on best-evidence
candidates rather than a published spec (RSSI/CLI/FET units, route-flag and
cost-field exact meanings, DLL type 8's leading flag bytes, MPDU type 17's
nested management/control TLVs, MPDU type 20's probable MLME security hash);
see the issue-3583 comment thread for the current reasoning behind each.

Flex decoder used during analysis:

    rtl_433 -s 1000k -X "n=ssnmesh,m=FSK_PCM,s=10,l=10,r=5000"
*/

#define SILVER_SPRING_PREAMBLE_BITLEN 32  /* "aa aa" + 16-bit sync */
#define SILVER_SPRING_PHR_BYTELEN      3  /* channel/seed, FCTRL/EXT/length-hi, length-lo */
#define SILVER_SPRING_FCS_BYTELEN      4  /* CRC-32 */
#define SILVER_SPRING_MAX_BYTELEN    512  /* largest complete frame seen is 283 bytes; PHR length field allows up to 2047+7 */

/* CRC-32/MPEG-2: poly 0x04c11db7, init 0xffffffff, no final XOR, MSB-first. */
static uint32_t silver_spring_mesh_crc32(uint8_t const *data, unsigned len)
{
    uint32_t crc = 0xffffffff;
    for (unsigned i = 0; i < len; ++i) {
        crc ^= (uint32_t)data[i] << 24;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04c11db7u : (crc << 1);
        }
    }
    return crc;
}

/* 8-bit additive scrambler x^8+x^4+x^3+x^2+1 (tap 0x8e), output bit 7,
   keystream applied MSB-first. XORs its keystream over buf in place. */
static void silver_spring_mesh_descramble(uint8_t *buf, unsigned len, uint8_t seed)
{
    uint8_t reg = seed;
    for (unsigned i = 0; i < len; ++i) {
        uint8_t k = 0;
        for (int b = 0; b < 8; ++b) {
            k = (uint8_t)((k << 1) | ((reg >> 7) & 1));
            uint8_t t = reg & 0x8e;
            t ^= t >> 4; t ^= t >> 2; t ^= t >> 1;
            reg = (uint8_t)((reg << 1) | (t & 1));
        }
        buf[i] ^= k;
    }
}

/* snprintf into out[*pos..out_sz), clamping *pos so it never exceeds out_sz
   even if the formatted text would have overflowed (further appends then
   safely no-op instead of leaving *pos out of bounds for the next call). */
static void silver_spring_mesh_appendf(char *out, size_t out_sz, size_t *pos, char const *fmt, ...)
{
    if (*pos >= out_sz) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + *pos, out_sz - *pos, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *pos += (size_t)n < out_sz - *pos ? (size_t)n : out_sz - *pos;
    }
}

/* Fields decoded from established (or best-evidence-candidate) TLVs while
   walking one PSDU's TLV stream. seen_* flags drive the poll/data/ack link
   classification; the rest are only valid when their have_* flag is set. */
typedef struct {
    int seen_dll2, seen_dll4, seen_dll9, seen_mpdu;

    int have_seq, retry, frag_more, frag_num, seq_num; /* DLL 4, Sequence Control */
    int have_fet, fet;                                 /* DLL 1, Fractional Epoch Tick */
    int have_rssi, rssi;                               /* DLL 9, signed RSSI candidate */
    int have_cli, cli_tx_pri, cli_tx_time, cli_rx_pri, cli_rx_time; /* DLL 2, CLI candidate */
    int have_sync, sync_channel;                       /* DLL 8, channel sync candidate */

    char routes[384]; /* MPDU 16 route advertisement object(s), if any */
    char ipv6[256];   /* MPDU 16 IPv6/UDP datagram, if any */
} silver_spring_extras_t;

/* Route advertisement object (network/routing patents, byte-local revision):
   version/type(1, always 0x21) | max-hop candidate(1) | prefix/control
   candidate(2, always 0x1000) | network address(16, always zero) | egress
   EUI-64(8) | descriptor(8: next-hop count(1) | route flags(3) | path-cost
   candidate(2) | link-cost candidate(2)) | next-hop EUI-64 * count(8 each).
   Total length = 36 + 8*count. Appends "hop<=H cost=P/L egress=<eui64>
   [next=<eui64> ...]"; returns bytes consumed, or 0 if truncated/malformed. */
static unsigned silver_spring_mesh_append_route(uint8_t const *buf, unsigned len, unsigned o, char *out, size_t out_sz, size_t *pos)
{
    if (o + 36 > len || buf[o] != 0x21) {
        return 0;
    }
    uint8_t count       = buf[o + 28];
    unsigned total      = 36 + 8u * count;
    if (o + total > len) {
        return 0;
    }
    unsigned path_cost = ((unsigned)buf[o + 32] << 8) | buf[o + 33];
    unsigned link_cost = ((unsigned)buf[o + 34] << 8) | buf[o + 35];

    silver_spring_mesh_appendf(out, out_sz, pos, "%shop<=%u cost=%u/%u egress=",
            *pos ? "; " : "", buf[o + 1], path_cost, link_cost);
    for (unsigned k = 0; k < 8; ++k) {
        silver_spring_mesh_appendf(out, out_sz, pos, "%02x", buf[o + 20 + k]);
    }
    for (unsigned n = 0; n < count; ++n) {
        silver_spring_mesh_appendf(out, out_sz, pos, " next=");
        for (unsigned k = 0; k < 8; ++k) {
            silver_spring_mesh_appendf(out, out_sz, pos, "%02x", buf[o + 36 + 8 * n + k]);
        }
    }
    return total;
}

static void silver_spring_mesh_parse_routes(uint8_t const *buf, unsigned len, char *out, size_t out_sz)
{
    out[0] = '\0';
    size_t pos = 0;
    unsigned o = 0;
    while (o < len) {
        unsigned used = silver_spring_mesh_append_route(buf, len, o, out, out_sz, &pos);
        if (!used) {
            break;
        }
        o += used;
    }
}

/* 16-bit Internet checksum (RFC 1071) folding helper: sum big-endian 16-bit
   words of data into running accumulator sum. */
static uint32_t silver_spring_mesh_ip_sum(uint32_t sum, uint8_t const *data, unsigned len)
{
    unsigned i;
    for (i = 0; i + 1 < len; i += 2) {
        sum += ((uint32_t)data[i] << 8) | data[i + 1];
    }
    if (i < len) {
        sum += (uint32_t)data[i] << 8;
    }
    return sum;
}

/* IPv6 (40-byte header) + UDP datagram, appends "[src] -> [dst] sport->dport
   len=N cksum=ok/bad"; when the destination port is 648 (the AMI FAN secure
   Management command port), also appends the declared cleartext length from
   the 12-byte clear envelope described by the traffic-analysis patent --
   without attempting to touch the still-unknown cipher/authentication suite. */
static void silver_spring_mesh_parse_ipv6(uint8_t const *p, unsigned len, char *out, size_t out_sz)
{
    out[0] = '\0';
    if (len < 40) {
        return;
    }
    unsigned plen        = ((unsigned)p[4] << 8) | p[5];
    uint8_t next_hdr     = p[6];
    uint8_t const *src   = p + 8;
    uint8_t const *dst   = p + 24;
    uint8_t const *body  = p + 40;
    if (40 + plen > len) {
        return;
    }

    size_t pos = 0;
    silver_spring_mesh_appendf(out, out_sz, &pos, "[");
    for (int i = 0; i < 16; i += 2) {
        silver_spring_mesh_appendf(out, out_sz, &pos, "%s%02x%02x", i ? ":" : "", src[i], src[i + 1]);
    }
    silver_spring_mesh_appendf(out, out_sz, &pos, "] -> [");
    for (int i = 0; i < 16; i += 2) {
        silver_spring_mesh_appendf(out, out_sz, &pos, "%s%02x%02x", i ? ":" : "", dst[i], dst[i + 1]);
    }
    silver_spring_mesh_appendf(out, out_sz, &pos, "]");

    if (next_hdr == 17 && plen >= 8) {
        unsigned sport = ((unsigned)body[0] << 8) | body[1];
        unsigned dport = ((unsigned)body[2] << 8) | body[3];
        unsigned ulen  = ((unsigned)body[4] << 8) | body[5];
        if (ulen <= plen) {
            uint32_t sum = 17; /* zero-padded next-header octet */
            sum += ulen;       /* 32-bit UDP length, upper 16 bits always 0 here */
            sum = silver_spring_mesh_ip_sum(sum, src, 16);
            sum = silver_spring_mesh_ip_sum(sum, dst, 16);
            sum = silver_spring_mesh_ip_sum(sum, body, ulen);
            while (sum >> 16) {
                sum = (sum & 0xffff) + (sum >> 16);
            }
            int cksum_ok = (sum & 0xffff) == 0xffff;
            silver_spring_mesh_appendf(out, out_sz, &pos, " %u->%u len=%u cksum=%s",
                    sport, dport, ulen, cksum_ok ? "ok" : "bad");
            if (dport == 648 && ulen >= 14) {
                unsigned mgmt_len = ((unsigned)body[12] << 8) | body[13];
                silver_spring_mesh_appendf(out, out_sz, &pos, " mgmt_len=%u", mgmt_len);
            }
        }
    }
}

/* MPDU type 16 payload: either a direct sequence of route advertisement
   objects (starting with the object's own 0x21 version/type byte), or a
   common network header (US20080051036A1, byte-local revision: version(4)|
   protocol_id(4), control(2)|TTL(6), control(2)|route-offset(6), priority(1)|
   source-route(1)|address-count(6), then that many EUI-64 addresses) wrapping
   a route-object payload (protocol id 3) or an IPv6 datagram (protocol id 6). */
static void silver_spring_mesh_parse_mpdu16(uint8_t const *v, unsigned vlen, silver_spring_extras_t *extras)
{
    if (vlen < 1) {
        return;
    }
    if (v[0] == 0x21) {
        silver_spring_mesh_parse_routes(v, vlen, extras->routes, sizeof(extras->routes));
        return;
    }
    if (vlen < 4) {
        return;
    }
    unsigned pid      = v[0] & 0x0f;
    unsigned addr_cnt = v[3] & 0x3f;
    unsigned hdr_len  = 4 + 8 * addr_cnt;
    if (hdr_len > vlen) {
        return;
    }
    uint8_t const *payload = v + hdr_len;
    unsigned payload_len    = vlen - hdr_len;
    if (pid == 3) {
        silver_spring_mesh_parse_routes(payload, payload_len, extras->routes, sizeof(extras->routes));
    }
    else if (pid == 6) {
        silver_spring_mesh_parse_ipv6(payload, payload_len, extras->ipv6, sizeof(extras->ipv6));
    }
}

/* Appends one TLV's "D<type>/<len>" or "M<type>/<len>" notation (matching
   the SS_MESH corpus analysis) for the record at psdu[i], recursing once for
   the nested TLV that MPDU type 17 always carries. Returns the number of
   bytes the record occupies (header + value), or 0 if truncated. Reports the
   parsed type/namespace via *out_type / *out_is_dll so the caller can detect
   the DLL type 5/6 stream terminators. extras may be NULL (used for the
   nested MPDU-17 TLV, whose own type/value semantics are not yet decoded);
   when non-NULL, established/candidate top-level TLV values are decoded into it. */
static unsigned silver_spring_mesh_append_tlv(uint8_t const *psdu, unsigned len, unsigned i,
        char *out, size_t out_sz, size_t *out_pos, unsigned *out_type, int *out_is_dll,
        silver_spring_extras_t *extras)
{
    if (i + 2 > len) {
        return 0;
    }
    uint8_t h0 = psdu[i];
    uint8_t h1 = psdu[i + 1];
    int is_mpdu   = (h0 & 0x80) != 0;
    unsigned type = is_mpdu ? (unsigned)(h0 >> 3) : (((unsigned)h0 << 1) | (h1 >> 7));
    unsigned vlen = is_mpdu ? (((unsigned)(h0 & 0x07) << 8) | h1) : (h1 & 0x7f);
    if (i + 2 + vlen > len) {
        return 0;
    }
    uint8_t const *val = psdu + i + 2;

    silver_spring_mesh_appendf(out, out_sz, out_pos, "%s%s%u/%u",
            *out_pos ? " " : "", is_mpdu ? "M" : "D", type, vlen);

    if (is_mpdu && type == 17 && vlen > 0) {
        unsigned nested_type;
        int nested_is_dll;
        silver_spring_mesh_appendf(out, out_sz, out_pos, "{");
        silver_spring_mesh_append_tlv(psdu, i + 2 + vlen, i + 2, out, out_sz, out_pos, &nested_type, &nested_is_dll, NULL);
        silver_spring_mesh_appendf(out, out_sz, out_pos, "}");
    }

    if (extras) {
        if (is_mpdu) {
            extras->seen_mpdu = 1;
            if (type == 16) {
                silver_spring_mesh_parse_mpdu16(val, vlen, extras);
            }
        }
        else switch (type) {
        case 2:
            extras->seen_dll2 = 1;
            if (vlen == 3) {
                uint32_t raw   = ((uint32_t)val[0] << 16) | ((uint32_t)val[1] << 8) | val[2];
                extras->have_cli    = 1;
                extras->cli_tx_pri  = (int)((raw >> 21) & 0x7);
                extras->cli_tx_time = (int)((raw >> 12) & 0x1ff);
                extras->cli_rx_pri  = (int)((raw >> 9) & 0x7);
                extras->cli_rx_time = (int)(raw & 0x1ff);
            }
            break;
        case 4:
            extras->seen_dll4 = 1;
            if (vlen == 2) {
                unsigned raw = ((unsigned)val[0] << 8) | val[1];
                extras->have_seq   = 1;
                extras->retry      = (int)((raw >> 15) & 1);
                extras->frag_more  = (int)((raw >> 14) & 1);
                extras->frag_num   = (int)((raw >> 10) & 0xf);
                extras->seq_num    = (int)(raw & 0x3ff);
            }
            break;
        case 9:
            extras->seen_dll9 = 1;
            if (vlen == 1) {
                extras->have_rssi = 1;
                extras->rssi      = val[0] >= 128 ? (int)val[0] - 256 : (int)val[0];
            }
            break;
        case 1:
            if (vlen == 2) {
                extras->have_fet = 1;
                extras->fet       = ((int)val[0] << 8) | val[1];
            }
            break;
        case 8:
            if (vlen == 5) {
                extras->have_sync     = 1;
                extras->sync_channel  = val[4];
            }
            break;
        default:
            break;
        }
    }

    *out_type   = type;
    *out_is_dll = !is_mpdu;
    return 2 + vlen;
}

/* Walks the DLL/MPDU TLV stream that follows FCTRL and the address(es)
   (US20090310511A1). DLL type 6 is a mid-stream marker, not a terminator:
   walking continues through any further TLVs, and only once the stream
   truly ends do the last 4 bytes get checked as a CRC-32/MPEG-2 over every
   preceding PSDU byte, appended as ":ok"/":bad". DLL type 5 (DLC End) does
   end the stream immediately, before a fragment's raw payload, which (like
   each TLV's value bytes) this decoder does not further parse; that and any
   truncated/unrecognized tail are reported as "+NB" (full bytes in `data`). */
static void silver_spring_mesh_parse_tlvs(uint8_t const *psdu, unsigned psdu_len, unsigned start,
        char *out, size_t out_sz, silver_spring_extras_t *extras)
{
    out[0] = '\0';
    size_t pos = 0;
    unsigned i = start;
    int saw_crc_marker = 0;

    while (i < psdu_len) {
        unsigned type;
        int is_dll;
        unsigned used = silver_spring_mesh_append_tlv(psdu, psdu_len, i, out, out_sz, &pos, &type, &is_dll, extras);
        if (!used) {
            break;
        }
        i += used;

        if (is_dll && type == 6) {
            saw_crc_marker = 1;
            continue;
        }
        if (is_dll && type == 5) {
            if (i < psdu_len) {
                silver_spring_mesh_appendf(out, out_sz, &pos, " +%uB", psdu_len - i);
            }
            return;
        }
    }

    if (saw_crc_marker && i + 4 == psdu_len) {
        uint32_t want = ((uint32_t)psdu[i] << 24) | ((uint32_t)psdu[i + 1] << 16)
                      | ((uint32_t)psdu[i + 2] << 8) | psdu[i + 3];
        uint32_t got  = silver_spring_mesh_crc32(psdu, i);
        silver_spring_mesh_appendf(out, out_sz, &pos, "%s:%s", pos ? " " : "", got == want ? "ok" : "bad");
    }
    else if (i < psdu_len) {
        silver_spring_mesh_appendf(out, out_sz, &pos, "%s+%uB", pos ? " " : "", psdu_len - i);
    }
}

/* Classifies the link-layer role from FCTRL and which TLV types the stream
   carried, per the MAC patent's four-packet poll/poll-ack/data/data-ack
   unicast procedure (plus FCTRL 2 broadcasts): FCTRL 2 is a broadcast; FCTRL
   3 is a poll; for FCTRL 1, Sequence Control (DLL 4) or any MPDU payload
   means data, a bare CLI (DLL 2) means a poll acknowledgment, and a bare
   RSSI (DLL 9) means a data acknowledgment. Returns "" if unclassifiable
   (e.g. an FCTRL 1 frame with no further TLVs at all). */
static char const *silver_spring_mesh_classify_link(uint8_t fctrl, silver_spring_extras_t const *extras)
{
    if (fctrl == 2) {
        return "broadcast";
    }
    if (fctrl == 3) {
        return "poll";
    }
    if (fctrl == 1) {
        if (extras->seen_dll4 || extras->seen_mpdu) {
            return "data";
        }
        if (extras->seen_dll2) {
            return "poll_ack";
        }
        if (extras->seen_dll9) {
            return "data_ack";
        }
    }
    return "";
}

static int silver_spring_mesh_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const sync_pattern[4] = {0xaa, 0xaa, 0x18, 0xbf};

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int row = 0;
    unsigned pos = bitbuffer_search(bitbuffer, row, 0, sync_pattern, SILVER_SPRING_PREAMBLE_BITLEN);
    if (pos >= bitbuffer->bits_per_row[row]) {
        bitbuffer_invert(bitbuffer);
        pos = bitbuffer_search(bitbuffer, row, 0, sync_pattern, SILVER_SPRING_PREAMBLE_BITLEN);
        if (pos >= bitbuffer->bits_per_row[row]) {
            return DECODE_ABORT_EARLY;
        }
    }

    /* The byte-aligned canonical frame begins at the LAST bit of the matched
       sync and is the bit-complement of the matched buffer from there. */
    unsigned frame_start = pos + SILVER_SPRING_PREAMBLE_BITLEN - 1;

    unsigned avail_bits = bitbuffer->bits_per_row[row] - frame_start;
    if (avail_bits < (SILVER_SPRING_PHR_BYTELEN + SILVER_SPRING_FCS_BYTELEN) * 8) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t frame[SILVER_SPRING_MAX_BYTELEN] = {0};
    unsigned avail_bytes = avail_bits / 8;
    if (avail_bytes > sizeof(frame)) {
        avail_bytes = sizeof(frame);
    }
    /* The canonical frame is the bit-complement of what was matched; invert
       once more so extraction yields canonical bytes directly. */
    bitbuffer_invert(bitbuffer);
    bitbuffer_extract_bytes(bitbuffer, row, frame_start, frame, avail_bytes * 8);

    if (frame[1] & 0xf8) {
        decoder_log(decoder, 1, __func__, "FCTRL/EXT mismatch, not a Silver Spring mesh frame");
        return DECODE_FAIL_SANITY;
    }

    int channel        = frame[0];
    unsigned psdu_len  = ((unsigned)(frame[1] & 0x07) << 8) | frame[2];
    unsigned frame_len = SILVER_SPRING_PHR_BYTELEN + psdu_len + SILVER_SPRING_FCS_BYTELEN;
    if (psdu_len < 1 || frame_len > avail_bytes) {
        decoder_logf(decoder, 1, __func__, "PSDU length %u inconsistent with %u captured bytes", psdu_len, avail_bytes);
        return DECODE_ABORT_LENGTH;
    }

    /* Descramble PSDU+FCS and verify CRC-32 by recovering the per-hop seed. */
    uint8_t psdu[SILVER_SPRING_MAX_BYTELEN];
    unsigned scr_len = psdu_len + SILVER_SPRING_FCS_BYTELEN;
    int seed_found   = -1;
    for (int seed = 1; seed < 256 && seed_found < 0; ++seed) {
        memcpy(psdu, frame + SILVER_SPRING_PHR_BYTELEN, scr_len);
        silver_spring_mesh_descramble(psdu, scr_len, (uint8_t)seed);
        uint32_t fcs = ((uint32_t)psdu[psdu_len] << 24) | ((uint32_t)psdu[psdu_len + 1] << 16)
                     | ((uint32_t)psdu[psdu_len + 2] << 8) | psdu[psdu_len + 3];
        if (silver_spring_mesh_crc32(psdu, psdu_len) == fcs) {
            seed_found = seed;
        }
    }
    if (seed_found < 0) {
        decoder_log(decoder, 1, __func__, "no scrambler seed yields a valid CRC-32");
        return DECODE_FAIL_MIC;
    }

    char psdu_str[SILVER_SPRING_MAX_BYTELEN * 2 + 1];
    for (unsigned i = 0; i < psdu_len; ++i) {
        snprintf(&psdu_str[i * 2], 3, "%02x", psdu[i]);
    }
    psdu_str[psdu_len * 2] = '\0';

    /* FCTRL (first PSDU byte): bit 0 selects a destination EUI-64, bit 1 a
       source EUI-64; when both are present, destination precedes source. */
    uint8_t fctrl = psdu[0];
    unsigned addr_off = 1;
    char dst_str[17] = "";
    char src_str[17] = "";
    if ((fctrl & 0x01) && addr_off + 8 <= psdu_len) {
        for (unsigned k = 0; k < 8; ++k) {
            snprintf(&dst_str[k * 2], 3, "%02x", psdu[addr_off + k]);
        }
        addr_off += 8;
    }
    if ((fctrl & 0x02) && addr_off + 8 <= psdu_len) {
        for (unsigned k = 0; k < 8; ++k) {
            snprintf(&src_str[k * 2], 3, "%02x", psdu[addr_off + k]);
        }
        addr_off += 8;
    }

    char tlv_str[256];
    silver_spring_extras_t extras = {0};
    silver_spring_mesh_parse_tlvs(psdu, psdu_len, addr_off, tlv_str, sizeof(tlv_str), &extras);
    char const *link = silver_spring_mesh_classify_link(fctrl, &extras);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",              DATA_STRING, "SilverSpring-Mesh",
            "dst_id",       "Destination EUI-64", DATA_COND, dst_str[0] != '\0', DATA_STRING, dst_str,
            "src_id",       "Source EUI-64", DATA_COND,   src_str[0] != '\0', DATA_STRING, src_str,
            "channel",      "RF channel",    DATA_INT,    channel,
            "seed",         "Scrambler seed",DATA_FORMAT, "%02x", DATA_INT, seed_found,
            "len",          "PSDU bytes",    DATA_INT,    (int)psdu_len,
            "link",         "Link role",     DATA_COND,   link[0] != '\0', DATA_STRING, link,
            "seq_num",      "Sequence num",  DATA_COND,   extras.have_seq, DATA_INT, extras.seq_num,
            "frag_num",     "Fragment num",  DATA_COND,   extras.have_seq, DATA_INT, extras.frag_num,
            "frag_more",    "More fragments",DATA_COND,   extras.have_seq, DATA_INT, extras.frag_more,
            "retry",        "Retry",         DATA_COND,   extras.have_seq, DATA_INT, extras.retry,
            "fet",          "Epoch tick",    DATA_COND,   extras.have_fet, DATA_INT, extras.fet,
            "rssi",         "RSSI",          DATA_COND,   extras.have_rssi, DATA_INT, extras.rssi,
            "cli_tx_pri",   "CLI tx prio",   DATA_COND,   extras.have_cli, DATA_INT, extras.cli_tx_pri,
            "cli_tx_time",  "CLI tx time",   DATA_COND,   extras.have_cli, DATA_INT, extras.cli_tx_time,
            "cli_rx_pri",   "CLI rx prio",   DATA_COND,   extras.have_cli, DATA_INT, extras.cli_rx_pri,
            "cli_rx_time",  "CLI rx time",   DATA_COND,   extras.have_cli, DATA_INT, extras.cli_rx_time,
            "sync_channel", "Sync channel",  DATA_COND,   extras.have_sync, DATA_INT, extras.sync_channel,
            "routes",       "Route adverts", DATA_COND,   extras.routes[0] != '\0', DATA_STRING, extras.routes,
            "ipv6",         "IPv6/UDP",      DATA_COND,   extras.ipv6[0] != '\0', DATA_STRING, extras.ipv6,
            "tlvs",         "TLV records",   DATA_COND,   tlv_str[0] != '\0', DATA_STRING, tlv_str,
            "data",         "PSDU",          DATA_STRING, psdu_str,
            "mic",          "Integrity",     DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields_silver_spring_mesh[] = {
        "model",
        "dst_id",
        "src_id",
        "channel",
        "seed",
        "len",
        "link",
        "seq_num",
        "frag_num",
        "frag_more",
        "retry",
        "fet",
        "rssi",
        "cli_tx_pri",
        "cli_tx_time",
        "cli_rx_pri",
        "cli_rx_time",
        "sync_channel",
        "routes",
        "ipv6",
        "tlvs",
        "data",
        "mic",
        NULL,
};

r_device const silver_spring_mesh = {
        .name        = "Silver Spring Networks mesh endpoint (-s 1600k)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 10,
        .long_width  = 10,
        .reset_limit = 1000,
        .decode_fn   = &silver_spring_mesh_decode,
        .fields      = output_fields_silver_spring_mesh,
};
