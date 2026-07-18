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

A CC1101-style GFSK packet: 13 us/bit (75 kbps), 0x55 preamble, then a
32 bit sync word 0xa723a723 -- except the real payload starts 1 bit
before the naive end of that 32 bit pattern (i.e. only the first 31
bits of the printed sync actually belong to the sync, the very next
bit is already the first payload bit). IBM/CCITT data whitening
(`ibm_whitening()`) follows, then:

    len cnt typ typ2 hop syst chl src(3) bwd(3) fwd(3) ndst dst(ndst) p1 p2 enc(8) crc(2)

- len: total following bytes, i.e. wire length is len+3 (this byte, then len bytes, then a 16 bit CRC)
- cnt: plain 8 bit rolling counter, incremented on every transmission
- typ, typ2, hop, syst: constant in every capture seen so far (0x45, 0x10, 0x05, 0x01)
- chl: 0x11/0x22/0x03 for channel 1/2/3, 0xff for the "all channels" button
- src, bwd, fwd: the sending remote's 24 bit address, repeated three times
- ndst, dst: a list of `ndst` destination channel bytes (same values as chl); the
  "all channels" button lists every channel (e.g. 11 22 03) instead of using 0xff
- p1, p2: constant (0x00, 0x03) in every capture seen so far
- enc: 8 byte obfuscated command block (see below)
- crc: CRC-16, poly 0x8005, init 0xffff, over every preceding byte including len

The `enc` block is NOT a checksum or a keyed rolling-code authenticator -- it's a
fixed, reversible obfuscation (nibble substitution table, then two decrementing-key
nibble subtractions with a byte-XOR step in between), taken from
QuadCorei8085/elero_protocol's xor_decode.py and verified here against 38
CRC-valid real captures across 4 channels: byte 2 of the decoded block is always
0x20 for Up, 0x10 for Stop, 0x40 for Down, with bytes 0-1 always zero.

GFSK at this deviation needs the `minmax` FSK pulse detector
(`-Y minmax`), which is not the default -- won't decode without it.
*/

// nibble substitution table, from QuadCorei8085/elero_protocol's xor_decode.py
static uint8_t const elero_nibble_table[16] = {
        0x0a, 0x03, 0x01, 0x0c, 0x0d, 0x07, 0x0f, 0x06,
        0x00, 0x08, 0x0b, 0x0e, 0x09, 0x02, 0x05, 0x04};

// decode the 8 byte obfuscated command block in place
static void elero_decode_command(uint8_t *msg)
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

static int elero_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // sync word 0xa723a723, but only the first 31 bits are actually sync,
    // the payload follows immediately (see docstring above)
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

    uint8_t frame[40] = {0};
    unsigned avail_bytes = avail_bits / 8;
    if (avail_bytes > sizeof (frame)) {
        avail_bytes = sizeof (frame);
    }
    bitbuffer_extract_bytes(bitbuffer, row, start_pos, frame, avail_bytes * 8);
    ibm_whitening(frame, avail_bytes);

    unsigned length = frame[0];
    unsigned total  = length + 3; // len byte + length bytes + 2 byte crc
    if (total > sizeof (frame) || avail_bytes < total) {
        return DECODE_ABORT_LENGTH; // truncated capture
    }

    uint16_t crc_calc = crc16(frame, total - 2, 0x8005, 0xffff);
    uint16_t crc_recv = (frame[total - 2] << 8) | frame[total - 1];
    if (crc_calc != crc_recv) {
        return DECODE_FAIL_MIC;
    }

    unsigned ndst = frame[16];
    if (17 + ndst + 2 + 8 + 2 > total) {
        return DECODE_FAIL_SANITY; // dst list doesn't fit the frame
    }

    int counter  = frame[1];
    uint32_t src = (uint32_t)frame[7] << 16 | (uint32_t)frame[8] << 8 | frame[9];

    char channel_str[11];
    char *p = channel_str;
    for (unsigned i = 0; i < ndst; ++i) {
        p += sprintf(p, "%02X", frame[17 + i]);
    }

    uint8_t enc[8];
    memcpy(enc, &frame[17 + ndst + 2], 8);
    elero_decode_command(enc);

    char const *command_str;
    switch (enc[2]) {
        case 0x20: command_str = "Up"; break;
        case 0x10: command_str = "Stop"; break;
        case 0x40: command_str = "Down"; break;
        default:   command_str = "?"; break;
    }

    char id_str[7];
    snprintf(id_str, sizeof (id_str), "%06X", src);

    /* clang-format off */
    data_t *data = data_make(
            "model",       "",             DATA_STRING, "Elero",
            "id",          "ID",           DATA_STRING, id_str,
            "channel",     "Channel",      DATA_STRING, channel_str,
            "command",     "Command",      DATA_STRING, command_str,
            "counter",     "Counter",      DATA_INT,    counter,
            "mic",         "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "command",
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
