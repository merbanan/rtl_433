/** @file
    Elster/Honeywell R2S/REXU family power meters.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Elster/Honeywell R2S/REXU family power meters.

These are frequency-hopping spread-spectrum (FHSS) meters, hopping across
25 channels 902.4-927.6 MHz, 400 kHz apart, FSK modulated with Manchester
coding. Since rtl_433 tunes a single frequency it will only ever catch a
fraction of the transmitted hops, but individual packets can still be
decoded when caught.

After the preamble and sync a Manchester-encoded frame follows, whitened
with a repeating 0x55 (i.e. every decoded byte is XORed with 0x55):

    LEN:8h FLAG:8h SRC:32h DST:32h ?:8h?:8h?:8h DATA:8h* CHK:16h

- LEN: number of bytes covered by the CRC, including this length byte
  itself but excluding the trailing CRC16
- FLAG: flags byte
- SRC: sending device address; top bit set marks a mesh-routing packet
  between meters/collectors rather than a plain meter->collector report
  (that whole sub-protocol, and the separate dst==0 flood-broadcast
  format, are not decoded here, only the common meter-report case)
  Matches the meter's printed "LAN ID", decimal (issue #1196, \@greyltc)
- DST: destination (collector) address, may be 0. -- id/dst are
  reported as decimal strings for this reason and to avoid a negative
  JSON int for addresses with the high bit set
- DATA: mostly unknown; a sub-command (length byte 0x33) at a fixed
  offset carries hourly usage data (command id 0xce), see below. Also
  reported in full as data_raw
- CHK: CRC-16/X-25 (poly 0x8408, init 0xffff, byte-reflected, complemented),
  transmitted low byte first, covering LEN through the last DATA byte

The hourly-usage (cmd 0xce) sub-payload layout, per decode_pcap.py:

    ?:8h CMD:8h CTR:8h ?:8h FLAG2:8h CUR_HOUR:16h LAST_HOUR:16h N:8h READING:16h*N

- CTR: a per-message counter/sequence number
- CUR_HOUR, LAST_HOUR: rolling hour counters bounding the readings below
- N: number of 16-bit hourly readings that follow (capped at 17)
- READING: hourly usage, scaled by 0.01

Since real captures show the preamble/sync region collapsing to a variable
number of bits depending on signal quality, this decoder does not rely on
a fixed sync offset: it Manchester-decodes the whole row (already done by
rtl_433's demod) and then brute-force searches every bit position for one
where the (whitened) first byte, taken as LEN, yields a valid CRC-16/X-25 --
false positive risk is negligible given the 16-bit check.

Findings from issue #1196 (\@ther3zz), live across ~15 meters:

- Type-1 splits into "beacon" (LEN=40, FLAG=0x08, DST=0, detected below
  as frame_type) and "unicast" (LEN=28, FLAG=0x00, nonzero DST, not
  distinguished here).
- A second message type ("type-2") exists, see elster_power_meter2 below.
  LEN=189/171 frames were being silently rejected by the old
  ELSTER_MAX_LEN=64 bound; raised to 200.
- Preamble measured at ~67 ms (~2400 chips), not ~21.8 ms as assumed;
  not applied to short_width/long_width, which already produce confirmed
  real decodes and may be for a different meter variant.

## Type-2 frames (elster_power_meter2, issue #3618, \@ther3zz)

Type-2 is a completely different physical framing from type-1 above: not
Manchester coded, whitened with a repeating 0xaa (not 0x55), a 16 bit
length field (not 8 bit), and a data rate 4x faster than type-1's ~28us
chips -- confirmed against real captures at 7 us/bit. Both types otherwise
share the same CRC-16/X-25 algorithm and a similar address layout:

    LEN:16h ?:8h SRC:32h DST:32h ?:8h DATA:8h* CHK:16h

- LEN: number of bytes from this field's own first byte through the last
  DATA byte, i.e. total on-wire bytes = LEN + 2 (the trailing CHK is not
  counted, unlike type-1's self-inclusive LEN)
- SRC, DST: same meaning and top-bit mesh convention as type-1
- DATA byte 4 (i.e. absolute offset 16) selects a message class in plain
  meter reports (SRC top bit clear): 0x56 = AES-ECB encrypted body (a
  per-meter key, not recoverable here -- reported as data_raw), 0x57 and
  0x7f = cleartext neighbour tables (issue #3618). Mesh/collector frames
  (SRC top bit set) don't use this byte the same way and are not further
  decoded, same as type-1
- Cleartext neighbour-table frames carry a record count N at a fixed
  offset followed by N 20-byte neighbour records; only the first 4 bytes
  of each record (the neighbour's own address) are confidently decoded
  here as nbr_ids, the remaining 16 bytes per record are of unconfirmed
  meaning and folded into data_raw instead of guessed at. 0x57 (seen at
  LEN=189) and 0x7f differ by one byte in where the records start
  relative to the count -- confirmed against real captures of both, not
  yet explained. A few other header bytes also differ between the two
  and are of unconfirmed meaning; treat 0x57 vs 0x7f as two distinct,
  independently-verified layouts rather than one
- Clear text meter readings arrive as submessage type 0x23 and are partially
  decoded here yielding a kWh meter reading and a timestamp for when
  the meter thinks it took the reading. Reporting the meter's
  timestamp is useful for precise calculation of delta t for computing
  average power (since the time our radio read the packet might not match
  the time the meter took the reading).
- Other cleartext frames (e.g. a LEN=38 status/heartbeat report,
  submessage type 0x0d) are reported via data_raw only -- little of their
  content varies between real captures beyond the sending meter's
  own address, and what does vary isn't confidently attributable
- CHK: same CRC-16/X-25 as type-1, but covering LEN through the last DATA
  byte using the *16 bit* LEN value (i.e. bytes[0:LEN], not LEN+2)

NB:
- Some jurisdictions seem to have enabled some sort of encryption (AES?)
which likely prevents the current codebase from correctly parsing those messages.
- My (\@greyltc's) meter can enter radio silence mode (as indicated by
the disappearance of a radio tower icon on the display) for long periods.
- Look for messages with a Source ID matching a number printed on the face of
your meter marked there as "LAN ID" or similar
  */

#define ELSTER_MIN_LEN 9   // at least LEN FLAG SRC(4) DST(4)
#define ELSTER_MAX_LEN 200 // sanity bound; longest confirmed real frame is a
                           // 189-byte mesh neighbour table (issue #1196)

static int elster_power_meter_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int row_bits = bitbuffer->bits_per_row[0];
    if (row_bits < (ELSTER_MIN_LEN + 2) * 8) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t const *row = bitbuffer->bb[0];
    uint8_t buf[ELSTER_MAX_LEN + 2];
    int found_pos = -1;
    int len       = 0;

    for (int pos = 0; pos + (ELSTER_MIN_LEN + 2) * 8 <= row_bits; ++pos) {
        int cand_len = bitrow_get_byte(row, pos) ^ 0x55;
        if (cand_len < ELSTER_MIN_LEN || cand_len > ELSTER_MAX_LEN) {
            continue;
        }
        if (pos + (cand_len + 2) * 8 > row_bits) {
            continue;
        }

        for (int i = 0; i < cand_len + 2; ++i) {
            buf[i] = bitrow_get_byte(row, pos + i * 8) ^ 0x55;
        }

        uint16_t chk      = crc16lsb(buf, cand_len, 0x8408, 0xffff) ^ 0xffff;
        uint16_t chk_recv = buf[cand_len] | (buf[cand_len + 1] << 8);
        if (chk == chk_recv) {
            found_pos = pos;
            len       = cand_len;
            break;
        }
    }

    if (found_pos < 0) {
        decoder_log(decoder, 2, __func__, "CRC fail");
        return DECODE_FAIL_MIC;
    }

    int flags   = buf[1];
    uint32_t src = ((uint32_t)buf[2] << 24) | (buf[3] << 16) | (buf[4] << 8) | buf[5];
    uint32_t dst = ((uint32_t)buf[6] << 24) | (buf[7] << 16) | (buf[8] << 8) | buf[9];

    // Decimal strings: matches the meter's printed LAN ID and avoids a
    // negative JSON int for addresses with the high bit set.
    char src_str[11];
    char dst_str[11];
    snprintf(src_str, sizeof(src_str), "%u", src);
    snprintf(dst_str, sizeof(dst_str), "%u", dst);

    // "Beacon" template, see file doc comment.
    int is_beacon = (len == 40 && flags == 0x08 && dst == 0);

    // Raw DATA region (after LEN/FLAG/SRC/DST, before the CRC).
    char data_raw[2 * (ELSTER_MAX_LEN - 10) + 1];
    data_raw[0]  = '\0';
    int data_len = len - 10;
    for (int i = 0; i < data_len; ++i) {
        snprintf(&data_raw[i * 2], 3, "%02x", buf[10 + i]);
    }

    int has_reading = 0;
    float meter_kwh = 0.0f;
    int has_hourly  = 0;
    int ctr         = 0;
    int cur_hour    = 0;
    int last_hour   = 0;
    char hourly_str[160];
    hourly_str[0] = '\0';

    // optional hourly-usage sub-command, s.a. reference decode_pcap.py;
    // only sent by plain meter reports, not mesh-routing packets
    if (!(src & 0x80000000) && len - 1 > 15) {
        int cmd_start = 15;
        int cmd_len   = buf[1 + cmd_start];
        if (cmd_len == 0x33 && len - 1 >= cmd_start + 1 + cmd_len) {
            uint8_t const *cmd = &buf[1 + cmd_start + 1];
            int cmd_id         = cmd[1];
            if (cmd_id == 0xce && cmd_len >= 10) {
                ctr         = cmd[2];
                cur_hour    = (cmd[5] << 8) | cmd[6];
                last_hour   = (cmd[7] << 8) | cmd[8];
                int n_hours = cmd[9];
                if (n_hours > 17) {
                    n_hours = 17;
                }
                has_hourly = 1;

                char *p = hourly_str;
                for (int h = 0; h < n_hours && cmd_len >= 10 + 2 * (h + 1); ++h) {
                    int raw = (cmd[10 + 2 * h] << 8) | cmd[10 + 2 * h + 1];
                    p += sprintf(p, "%s%.2f", h ? "," : "", raw * 0.01f);
                }
            }
            // unconfirmed even by decode_pcap.py, the more careful reference dissector
            if (cmd_id == 0xce && cmd_len >= 47) {
                uint32_t reading_raw = ((uint32_t)cmd[44] << 16) | (cmd[45] << 8) | cmd[46];
                meter_kwh            = (float)reading_raw;
                has_reading          = 1;
            }
        }
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "Elster-PowerMeter",
            "id",           "Meter ID",     DATA_STRING, src_str,
            "dst",          "Collector ID (LAN ID)", DATA_STRING, dst_str,
            "flags",        "Flags",        DATA_FORMAT, "%02x", DATA_INT, flags,
            "frame_type",   "Frame Type",   DATA_COND, is_beacon,   DATA_STRING, "beacon",
            "ctr",          "Counter",      DATA_COND, has_hourly,  DATA_INT, ctr,
            "cur_hour",     "Current Hour", DATA_COND, has_hourly,  DATA_INT, cur_hour,
            "last_hour",    "Last Hour",    DATA_COND, has_hourly,  DATA_INT, last_hour,
            "hourly_kWh",   "Hourly",       DATA_COND, has_hourly,  DATA_STRING, hourly_str,
            "reading_kWh",  "Reading",      DATA_COND, has_reading, DATA_FORMAT, "%.0f kWh", DATA_DOUBLE, (double)meter_kwh,
            "data_raw",     "Undecoded data", DATA_STRING, data_raw,
            "mic",          "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "dst",
        "flags",
        "frame_type",
        "ctr",
        "cur_hour",
        "last_hour",
        "hourly_kWh",
        "reading_kWh",
        "data_raw",
        "mic",
        NULL,
};

r_device const elster_power_meter = {
        .name        = "Elster/Honeywell R2S/REXU power meter",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 28,
        .long_width  = 28,
        .reset_limit = 3000,
        .decode_fn   = &elster_power_meter_decode,
        .fields      = output_fields,
};

#define ELSTER2_MIN_LEN 12  // at least LEN(2) ?(1) SRC(4) DST(4) ?(1)
#define ELSTER2_MAX_LEN 200 // sanity bound; longest confirmed real frame is a
                            // 189-byte neighbour table (issue #3618)
#define ELSTER2_NBR_MAX 8   // sanity bound on the neighbour-table record count
#define ELSTER2_COLLECTOR_BITMASK 0x80000000 // collector ID indication bit

struct __attribute__((packed, scalar_storage_order("big-endian"))) Header {
    uint16_t len;     // [0,1] packet length (counts _all_ bytes in the message, including these length bytes)
    uint8_t flags;    // [2] flags? broadcast/unicast? always 0x09 or 0x01?
    uint32_t src;     // [3,7] origin address
    uint32_t dst;     // [8,11] destination address
    uint8_t network;  // [12] network prefix
};

struct __attribute__((packed, scalar_storage_order("little-endian"))) SubHeader {
    uint8_t len;      // [0] length (count starts the byte after this one)
    uint8_t flags;    // [1] flags?
    uint8_t cls;      // [2] subclass
};

struct __attribute__((packed, scalar_storage_order("little-endian"))) Structure23 {
    uint8_t uk0[6];  // unknown
    uint8_t YY;
    uint8_t MM;
    uint8_t DD;
    uint8_t hh;
    uint8_t mm;
    uint8_t ss;
    uint8_t unknown1;
    uint32_t readinga1000x;
    uint32_t readingb1000x;
    uint32_t readingc1000x;
    uint32_t readingd1000x;
};

/** @fn static int elster_power_meter2_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Elster/Honeywell R2S/REXU power meter, type-2 frames, see issue #3618.
*/
static int elster_power_meter2_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int row_bits = bitbuffer->bits_per_row[0];
    if (row_bits < (ELSTER2_MIN_LEN + 2) * 8) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t const *row = bitbuffer->bb[0];
    uint8_t buf[ELSTER2_MAX_LEN + 2];
    int found_pos = -1;
    int len       = 0;
    int len_hi;
    int len_lo;
    int cand_len;

    for (int pos = 0; pos + (ELSTER2_MIN_LEN + 2) * 8 <= row_bits; ++pos) {
        len_hi = bitrow_get_byte(row, pos) ^ 0xaa;
        len_lo = bitrow_get_byte(row, pos + 8) ^ 0xaa;
        cand_len = (len_hi << 8) | len_lo;
        if (cand_len < ELSTER2_MIN_LEN || cand_len > ELSTER2_MAX_LEN) {
            continue;
        }
        if (pos + (cand_len + 2) * 8 > row_bits) {
            continue;
        }

        for (int i = 0; i < cand_len + 2; ++i) {
            buf[i] = bitrow_get_byte(row, pos + i * 8) ^ 0xaa;
        }

        // CRC covers the 16 bit LEN value itself (bytes[0:len]), not len+2
        uint16_t chk      = crc16lsb(buf, cand_len, 0x8408, 0xffff) ^ 0xffff;
        uint16_t chk_recv = buf[cand_len] | (buf[cand_len + 1] << 8);
        if (chk == chk_recv) {
            found_pos = pos;
            len       = cand_len;
            break;
        }
    }

    if (found_pos < 0) {
        decoder_log(decoder, 2, __func__, "CRC fail");
        return DECODE_FAIL_MIC;
    }

    struct Header* header;
    struct SubHeader* sh;
    uint32_t src = 0;
    int from_collector = 0;
    uint32_t dst = 0;
    int to_collector = 0;
    uint8_t network = 0;
    char src_str[15] = {0};
    char dst_str[15] = {0};
    uint8_t cls = 0x00;
    uint8_t *pl;  // payload
    //uint8_t scls = 0x00;  // subclass
    int remaining = len;

    struct Structure23* struct_23;
    int good_reading = 0;
    char reading_timestamp[20] = {0};
    float metera_kwh = 0.0f;
    float meterb_kwh = 0.0f;
    float meterc_kwh = 0.0f;
    float meterd_kwh = 0.0f;

    if ((unsigned int)len >= sizeof(struct Header)){
        header = (struct Header*)buf;
        remaining -= sizeof(*header);

        decoder_logf(decoder, 2, __func__, "packet len = %u", len);
        decoder_logf(decoder, 2, __func__, "header len = %u", header->len);
        decoder_logf(decoder, 2, __func__, "len mismatch = %u", (header->len != len));
        decoder_logf(decoder, 2, __func__, "flags = 0x%02x", header->flags);

        src = header->src;
        from_collector = (src & ELSTER2_COLLECTOR_BITMASK) != 0;
        decoder_logf(decoder, 2, __func__, "from_collector = %u", from_collector);
        if (from_collector){  // this packet is from a collector-type node
            src ^= ELSTER2_COLLECTOR_BITMASK;
        }

        dst = header->dst;
        to_collector = (dst & ELSTER2_COLLECTOR_BITMASK) != 0;
        decoder_logf(decoder, 2, __func__, "to_collector = %u", to_collector);
        if (to_collector){  // this packet is to a collector-type node
            dst ^= ELSTER2_COLLECTOR_BITMASK;
        }

        network = header->network;

        // this format exactly matches the "LAN-ID" marking that may appear on a meter face
        snprintf(src_str, sizeof(src_str), "%u-%010u", network, src);
        snprintf(dst_str, sizeof(dst_str), "%u-%010u", network, dst);

        if (remaining) {
            pl = &buf[sizeof(*header)];  // payload begins immediately after header

            if ((header->flags == 0x09) && from_collector && !to_collector) {
                // messages from collector nodes have an unknown format

                // I think the neighbor table decoding stuff will end up living here

                /*
                // Neighbour table: msg 0x57 or 0x7f, a record count at a fixed offset
                // followed by that many 20 byte records (only the leading 4-byte
                // neighbour address of each is confidently decoded, see file doc
                // comment). The two message classes start their records one byte
                // apart, confirmed against real captures of both.
                char nbr_ids[ELSTER2_NBR_MAX * 9] = "";
                if ((msg == 0x57 || msg == 0x7f) && len > 30) {
                    int n         = buf[28];
                    int rec_start = (msg == 0x57) ? 30 : 29;
                    if (n > 0 && n <= ELSTER2_NBR_MAX && rec_start + n * 20 <= len) {
                        char *p = nbr_ids;
                        for (int i = 0; i < n; ++i) {
                            uint8_t const *nbr = &buf[rec_start + i * 20];
                            p += sprintf(p, "%s%02x%02x%02x%02x", i ? "," : "",
                                    nbr[0], nbr[1], nbr[2], nbr[3]);
                        }
                    }
                }
                */
            } else if ((header->flags == 0x01) && !from_collector && to_collector && remaining >= 5) {
                decoder_logf(decoder, 2, __func__, "Pre-class unknown = 0x%02x%02x%02x%02x", pl[0], pl[1], pl[2], pl[3]);
                cls = pl[4];  // this often ends up being 0x56 or 0x57, but I'm not sure we really care. sh->cls (below) seems more important
                remaining -= 5;
                decoder_logf(decoder, 2, __func__, "class = 0x%02x", cls);
                if (remaining >= 5 + sizeof(*sh)) {
                    decoder_logf(decoder, 2, __func__, "Post-class unknown = 0x%02x%02x%02x%02x%02x", pl[5], pl[6], pl[7], pl[8], pl[9]);
                    remaining -= 5;
                    sh = (struct SubHeader*)&pl[10];
                    remaining -= 3;
                    decoder_logf(decoder, 2, __func__, "Sub-length = %u", sh->len);
                    decoder_logf(decoder, 2, __func__, "Sub-flags = 0x%02x", sh->flags);
                    decoder_logf(decoder, 2, __func__, "Sub-class = 0x%02x", sh->cls);
                    decoder_logf(decoder, 2, __func__, "Leftover = %i", header->len - sh->len);
                    switch (sh->cls) {  // branch on the message class byte in the sub-header
                        case 0x23:  // meter reading type. length = 164 or 171
                            if ((sh->len == 164) || (sh->len == 171)){
                                if (remaining >= sizeof(*struct_23)) {
                                    struct_23 = (struct Structure23*)&pl[13];
                                    remaining -= sizeof(*struct_23);
                                    metera_kwh = ((float) struct_23->readinga1000x) / 1000.0;
                                    meterb_kwh = ((float) struct_23->readingb1000x) / 1000.0;
                                    meterc_kwh = ((float) struct_23->readingc1000x) / 1000.0;
                                    meterd_kwh = ((float) struct_23->readingd1000x) / 1000.0;

                                    if (struct_23->readinga1000x != 0) {
                                        good_reading = 1;
                                        // report meter reading timestamp using format: YYY-MM-DDThh:mm:ss
                                        snprintf(reading_timestamp, sizeof(reading_timestamp), "%04u-%02u-%02uT%02u:%02u:%02u", struct_23->YY + 2000, struct_23->MM, struct_23->DD, struct_23->hh, struct_23->mm, struct_23->ss);
                                    }
                                }
                            } else if (sh->len == 159) {  // false meter reading type
                                (void)0;
                            } else {  // never seen this
                                (void)0;
                            }
                            break;
                        case 0x0d:  // heartbeat/beacon type? length = 7
                            break;
                        case 0xa4:  // unknown
                            break;
                        case 0x01:  // unknown
                            break;
                        case 0x20:  // unknown, length = 52
                            break;
                        case 0x0f:  // unknown
                            break;
                        case 0x33:  // unknown
                            break;
                        case 0x28:  // unknown
                            break;
                        case 0x05:  // unknown
                            break;
                        case 0x18:  // unknown, length = 164
                            break;
                        default:
                            (void)0;
                    }
                }
            }
        }
    }

    // __All__ the bytes (not including CRC)
    char data_raw[2 * ELSTER2_MAX_LEN + 1];
    data_raw[0]  = '\0';
    for (int i = 0; i < len; ++i) {
        snprintf(&data_raw[i * 2], 3, "%02x", buf[i]);
    }


    /* clang-format off */
    data_t *data = data_make(
            "model",    "",                      DATA_STRING, "Elster-PowerMeter2",
            "len",      "Message Length",        DATA_INT, len,
//            "flags",    "",                      DATA_STRING, flags_str,
            "src",      "Source ID",             DATA_STRING, src_str,
            "fm_coll",  "From Collector",        DATA_INT, from_collector,
            "dst",      "Destination ID",        DATA_STRING, dst_str,
            "to_coll",  "To Collector",          DATA_INT, to_collector,
            "mic",      "Integrity",             DATA_STRING, "CRC",
//            "scls",     "Message Subclass",      DATA_COND, scls > 0, DATA_STRING, scls_str,
//            "smsg_len", "Submessage Length",     DATA_COND, smsg_len > 0, DATA_INT, smsg_len,
//            "nbr_ids",  "Neighbour IDs",         DATA_COND, nbr_ids[0] != '\0', DATA_STRING, nbr_ids,
            "t_meter",  "Reading Timestamp",     DATA_COND, good_reading, DATA_STRING, reading_timestamp,  // "YYY-MM-DDThh:mm:ss"
            "readinga_kWh", "Reading A",         DATA_COND, good_reading, DATA_FORMAT, "%.3f kWh", DATA_DOUBLE, (double)metera_kwh,
            "readingb_kWh", "Reading B",         DATA_COND, good_reading, DATA_FORMAT, "%.3f kWh", DATA_DOUBLE, (double)meterb_kwh,
            "readingc_kWh", "Reading C",         DATA_COND, good_reading, DATA_FORMAT, "%.3f kWh", DATA_DOUBLE, (double)meterc_kwh,
            "readingd_kWh", "Reading D",         DATA_COND, good_reading, DATA_FORMAT, "%.3f kWh", DATA_DOUBLE, (double)meterd_kwh,
            "data_raw", "Raw data",              DATA_STRING, data_raw,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields2[] = {
        "model",
        "len",
//        "flags",
        "src",
        "fm_coll",
        "dst",
        "to_coll",
        "mic",
//        "smsg",
//        "smsg_len",
//        "nbr_ids",
        "t_meter",
        "readinga_kWh",
        "readingb_kWh",
        "readingc_kWh",
        "readingd_kWh",
        "data_raw",
        NULL,
};

r_device const elster_power_meter2 = {
        .name        = "Elster/Honeywell R2S/REXU power meter, type-2 frames",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 7,
        .long_width  = 7,
        .reset_limit = 4000,
        .decode_fn   = &elster_power_meter2_decode,
        .fields      = output_fields2,
};
