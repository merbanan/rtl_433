/** @file
    Elero bidirectional 868/915 MHz blinds/awning remote protocol.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn static int elero_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Elero bidirectional 868/915 MHz blinds/awning remote protocol.

Used by Elero-based blinds/awning/curtain motor remotes, e.g. the
Silent Gliss 5600/11490-series wall switch. Reverse engineered in
issue #3083 (https://github.com/merbanan/rtl_433/issues/3083), from
real captures and cross-checked against the independent
QuadCorei8085/elero_protocol and andyboeh/esphome-elero projects.

The CC1101 uses GFSK at 76.8 kbit/s (about 13 us/bit), a 0x55 preamble
and the sync word 0xd391d391. The decoder matches the last 31 sync bits.
After removal of IBM data whitening, the packet layout is:

    len cnt typ typ2 hop syst chl src(3) bwd(3) fwd(3) ndst dst(variable) p1 p2 enc(8) crc(2)

- len: total following bytes, i.e. wire length is len+3 (this byte, then len bytes, then a 16 bit CRC)
- cnt: sender counter. Repeated copies can have the same counter.
- typ: 0x44/0x45/0x70 use one-byte channel selectors. Commands (0x69/0x6a)
  and motor replies (0xc9/0xca) use three-byte destination addresses.
- chl: header channel. On the Silent Gliss remote, the all-channel button
  sets this to 0xff and lists the individual channels in dst. In recorded
  0xca replies, this byte echoes the command counter.
- src, bwd, fwd: 24-bit source and relay addresses
- ndst, dst: destination count followed by that many selectors or addresses
- p1, p2: two prefix bytes
- enc: eight-byte encoded block. Decoded byte 2 holds the command.
  On motor replies, decoded byte 6 holds the state.
- crc: CRC-16, poly 0x8005, init 0xffff, over every preceding byte including len

The `enc` block is NOT a checksum or a keyed rolling-code authenticator -- it's a
fixed, reversible obfuscation (nibble substitution table, then two decrementing-key
nibble subtractions with a byte-XOR step in between), taken from
QuadCorei8085/elero_protocol's xor_decode.py and verified here against 38
CRC-valid real captures across 4 channels: byte 2 of the decoded block is always
0x20 for Up, 0x10 for Stop, 0x40 for Down, with bytes 0-1 always zero.

The addressed layout and payload fields are documented in
https://github.com/manuschillerdev/esphome-elero/blob/c12eb4484ac1aa7b4662d2148a765e860b86b2d3/docs/PACKET_STRUCTURE.md

The default FSK detector and `-Y minmax` both decode the Silent Gliss recordings.
*/

// nibble substitution table, from QuadCorei8085/elero_protocol's xor_decode.py
static uint8_t const elero_nibble_table[16] = {
        0x0a, 0x03, 0x01, 0x0c, 0x0d, 0x07, 0x0f, 0x06,
        0x00, 0x08, 0x0b, 0x0e, 0x09, 0x02, 0x05, 0x04};

// Decode the eight-byte payload in place.
static void elero_decode_payload(uint8_t *msg)
{
    for (int i = 0; i < 8; ++i) {
        uint8_t nh = elero_nibble_table[(msg[i] >> 4) & 0xf];
        uint8_t nl = elero_nibble_table[msg[i] & 0xf];
        msg[i]     = (nh << 4) | nl;
    }

    uint8_t key = 0xfe;
    for (int i = 0; i < 2; ++i) {
        uint8_t ln = (msg[i] - key) & 0x0f;
        uint8_t hn = (msg[i] & 0xf0) - (key & 0xf0);
        msg[i]     = hn | ln;
        key -= 0x22;
    }

    uint8_t xor_b0 = msg[0];
    uint8_t xor_b1 = msg[1];
    for (int i = 0; i < 8; i += 2) {
        msg[i]     ^= xor_b0;
        msg[i + 1] ^= xor_b1;
    }

    key = 0xba;
    for (int i = 2; i < 8; ++i) {
        uint8_t ln = (msg[i] - key) & 0x0f;
        uint8_t hn = (msg[i] & 0xf0) - (key & 0xf0);
        msg[i]     = hn | ln;
        key -= 0x22;
    }
}

static char const *elero_command(unsigned type, uint8_t code)
{
    switch (code) {
        case 0x00:
            if (type == 0x44)
                return "Release";
            if (type == 0x69 || type == 0x6a)
                return "Status request";
            return NULL;
        case 0x10: return "Stop";
        case 0x20: return "Up";
        case 0x21: return "Continuous up";
        case 0x24: return "Tilt";
        case 0x40: return "Down";
        case 0x41: return "Continuous down";
        case 0x44: return "Intermediate";
        default:   return NULL;
    }
}

static char const *elero_state(uint8_t code)
{
    static char const *const states[] = {
            "Unknown", "Top", "Bottom", "Intermediate", "Tilt", "Blocked", "Overheated", "Timeout",
            "Starting up", "Starting down", "Moving up", "Moving down", "Delete in remote", "Stopped",
            "Top at tilt position", "Bottom at intermediate position", "Off", "On",
    };
    return code < sizeof(states) / sizeof(states[0]) ? states[code] : "?";
}

static int elero_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Last 31 bits of 0xd391d391, left-aligned for bitbuffer_search().
    uint8_t const sync[]  = {0xa7, 0x23, 0xa7, 0x23};
    unsigned const sync_bits = 31;

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int row = 0;
    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, sync, sync_bits);
    if (start_pos == bitbuffer->bits_per_row[row]) {
        return DECODE_ABORT_EARLY;
    }
    start_pos += sync_bits;

    unsigned avail_bits = bitbuffer->bits_per_row[row] - start_pos;
    if (avail_bits < 8) {
        return DECODE_ABORT_LENGTH;
    }

    // The supported 57-byte body contains 26 fixed bytes plus the destinations.
    enum { MAX_BODY = 57, FIXED_BODY = 26, MAX_DEST_BYTES = MAX_BODY - FIXED_BODY };
    uint8_t frame[1 + MAX_BODY + 2];
    unsigned avail_bytes = avail_bits / 8;
    if (avail_bytes > sizeof (frame)) {
        avail_bytes = sizeof (frame);
    }
    bitbuffer_extract_bytes(bitbuffer, row, start_pos, frame, avail_bytes * 8);
    ibm_whitening(frame, avail_bytes);

    unsigned length = frame[0];
    unsigned total  = length + 3; // len byte + length bytes + 2 byte crc
    if (length < FIXED_BODY || avail_bytes < total) {
        return DECODE_ABORT_LENGTH;
    }

    uint16_t crc_calc = crc16(frame, total - 2, 0x8005, 0xffff);
    uint16_t crc_recv = (frame[total - 2] << 8) | frame[total - 1];
    if (crc_calc != crc_recv) {
        return DECODE_FAIL_MIC;
    }

    unsigned type = frame[2];
    int status    = type == 0xc9 || type == 0xca;
    int addressed = status || type == 0x69 || type == 0x6a;
    if (!addressed && type != 0x44 && type != 0x45 && type != 0x70) {
        return DECODE_FAIL_SANITY;
    }

    unsigned ndst       = frame[16];
    unsigned dest_bytes = ndst * (addressed ? 3 : 1);
    if (!ndst || FIXED_BODY + dest_bytes > length) {
        return DECODE_FAIL_SANITY;
    }

    int counter  = frame[1];
    uint32_t src = (uint32_t)frame[7] << 16 | (uint32_t)frame[8] << 8 | frame[9];

    char channel_str[2 * MAX_DEST_BYTES + 1];
    if (addressed) {
        snprintf(channel_str, sizeof(channel_str), "%02X", frame[6]);
    }
    else {
        for (unsigned i = 0; i < ndst; ++i) {
            snprintf(channel_str + 2 * i, 3, "%02X", frame[17 + i]);
        }
    }

    uint8_t enc[8];
    memcpy(enc, &frame[17 + dest_bytes + 2], sizeof(enc));
    elero_decode_payload(enc);

    char id_str[7];
    snprintf(id_str, sizeof (id_str), "%06X", src);

    decoder_logf(decoder, 1, __func__, "type %02x/%02x hop %02x header[6] %02x bwd %02x%02x%02x fwd %02x%02x%02x",
            type, frame[3], frame[4], frame[6], frame[10], frame[11], frame[12], frame[13], frame[14], frame[15]);

    char const *command = elero_command(type, enc[2]);
    /* clang-format off */
    data_t *data = data_make(
            "model",           "",                DATA_STRING, "Elero",
            "id",              "ID",              DATA_STRING, id_str,
            "channel",         "Channel",         DATA_COND, !status, DATA_STRING, channel_str,
            "request_counter", "Request counter", DATA_COND, type == 0xca, DATA_INT, frame[6],
            "command",         "Command",         DATA_COND, !status, DATA_STRING, command ? command : "?",
            "command_code",    "Command code",    DATA_COND, !status && (addressed || !command), DATA_INT, enc[2],
            "state",           "State",           DATA_COND, status, DATA_STRING, elero_state(enc[6]),
            "state_code",      "State code",      DATA_COND, status, DATA_INT, enc[6],
            "counter",         "Counter",         DATA_INT,    counter,
            "mic",             "Integrity",       DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    if (addressed) {
        char addresses[MAX_DEST_BYTES / 3][7];
        char *destinations[MAX_DEST_BYTES / 3];
        for (unsigned i = 0; i < ndst; ++i) {
            uint8_t const *dst = &frame[17 + 3 * i];
            snprintf(addresses[i], sizeof(addresses[i]), "%02X%02X%02X", dst[0], dst[1], dst[2]);
            destinations[i] = addresses[i];
        }
        data = data_ary(data, "destinations", "Destinations", NULL, data_array(ndst, DATA_STRING, destinations));
    }
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "request_counter",
        "destinations",
        "command",
        "command_code",
        "state",
        "state_code",
        "counter",
        "mic",
        NULL,
};

r_device const elero = {
        .name        = "Elero bidirectional blinds/awning remote (Silent Gliss and others)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 13,
        .long_width  = 13,
        .reset_limit = 4000,
        .decode_fn   = &elero_decode,
        .fields      = output_fields,
};
