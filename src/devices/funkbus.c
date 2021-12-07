/** @file
    Funkbus / Instafunk.

    Copyright (C) 2021 Markus Sattler

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Funkbus / Instafunk
used by Berker, Jira, Jung and may more
developed by Insta GmbH.

- Frequency: 433.92Mhz
- Preamble: 4000us
- Short: 500us
- Long: 1000us
- Encoding: Differential Manchester Biphase–Mark (BP-M)

      __ __       __    __ __    __
     |     |     |  |  |     |  |  |
    _|     |__ __|  |__|     |__|  |__.....
     |  0  |  0  | 	1  |  0  |  1  |

- Mic: parity + lfsr with 8bit mask 0x8C shifted left by 2 bit
- Bits: 48
- Endian: LSB

Data layout:

    TS II II IF FA AX

- T: 4 bit type, there are multible types
- S: 4 bit subtype
- I: 20 bit serial number
- F: 2 bit r1, unknown
- F: 1 bit bat, 1 == battery low
- F: 2 bit r2,  // unknown
- F: 3 bit command, button on the remote
- A: 2 bit group, remote channel group 0-2 (A-C) are switches, 3 == light scene
- A: 1 bit r3, unknown
- A: 2 bit action
- A: 1 bit repeat, 1 == not first send of packet
- A: 1 bit longpress, longpress of button for (dim up/down, scene lerning)
- A: 1 bit parity, parity over all bits before
- X: 4 bit check, LFSR with 8 bit mask 0x8C shifted left by 2 each bit

Some details can be found by searching  "instafunk RX/TX-Modul pdf".
*/

#include "decoder.h"
#include <limits.h>

#define BIT_MASK(x) \
    ((((unsigned)x) >= sizeof(unsigned) * CHAR_BIT) ? (unsigned)-1 : (1U << (x)) - 1)

typedef enum {
    FB_ACTION_STOP,
    FB_ACTION_OFF,
    FB_ACTION_ON,
    FB_ACTION_SCENE
} funkbus_action_t;

typedef struct {
    uint8_t typ : 4; // there are multible types
    uint8_t subtyp : 4;

    uint32_t sn : 20;

    uint8_t r1 : 2;  // unknown
    uint8_t bat : 1; // 1 == battery low
    uint8_t r2 : 2;  // unknown

    uint8_t command : 3; // button on the remote
    uint8_t group : 2;   // remote channel group 0-2 (A-C) are switches, 3 == light scene
    uint8_t r3 : 1;      // unknown

    funkbus_action_t action : 2;
    uint8_t repeat : 1;    // 1 == not first send of packet
    uint8_t longpress : 1; // longpress of button for (dim up/down, scene lerning)
    uint8_t parity : 1;    // parity over all bits before
    uint8_t check : 4;     // lfsr with 8bit mask 0x8C shifted left by 2 each bit
} __attribute__((packed)) funkbus_packet_t;

static uint32_t get_bits_reflect(uint8_t const *bitrow, unsigned start, unsigned len)
{
    unsigned end = start + len - 1;
    uint32_t result = 0;
    uint32_t mask   = 1;
    result          = 0;
    for (; start <= end; mask <<= 1)
        if (bitrow_get_bit(bitrow, start++) != 0)
            result |= mask;
    return result;
}

static uint8_t calc_checksum(uint8_t const *bitrow, unsigned len)
{
    const uint8_t full_bytes = len / 8;
    const uint8_t bits_left  = len % 8;

    uint8_t xor = xor_bytes(bitrow, full_bytes);
    if (bits_left) {
        xor ^= bitrow[full_bytes] & ~BIT_MASK(8 - bits_left);
    }

    const uint8_t xor_nibble = ((xor&0xF0) >> 4) ^ (xor&0x0F);

    uint8_t result = 0;
    if (xor_nibble & 0x8) {
        result ^= 0x8C;
    }
    if (xor_nibble & 0x4) {
        result ^= 0x32;
    }
    if (xor_nibble & 0x2) {
        result ^= 0xC8;
    }
    if (xor_nibble & 0x01) {
        result ^= 0x23;
    }

    result = result & 0xF;
    result |= (parity8(xor) << 4);

    return result;
}

static int funkbus_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int events = 0;

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        if (bitbuffer->bits_per_row[row] < 48) {
            return DECODE_ABORT_LENGTH;
        }

        uint8_t *b = bitbuffer->bb[row];

        funkbus_packet_t packet;
        packet.typ    = get_bits_reflect(b, 0, 4);
        packet.subtyp = get_bits_reflect(b, 4, 4);

        // only handle packet typ for remotes
        if (packet.typ != 0x4 || packet.subtyp != 0x3) {
            return DECODE_ABORT_EARLY;
        }

        packet.sn        = get_bits_reflect(b, 8, 20);
        packet.r1        = get_bits_reflect(b, 28, 2);
        packet.bat       = get_bits_reflect(b, 30, 1);
        packet.r2        = get_bits_reflect(b, 31, 2);
        packet.command   = get_bits_reflect(b, 33, 3);
        packet.group     = get_bits_reflect(b, 36, 2);
        packet.r3        = get_bits_reflect(b, 38, 1);
        packet.action    = get_bits_reflect(b, 39, 2);
        packet.repeat    = get_bits_reflect(b, 41, 1);
        packet.longpress = get_bits_reflect(b, 42, 1);
        packet.parity    = get_bits_reflect(b, 43, 1);
        packet.check     = get_bits_reflect(b, 44, 4);

        uint8_t checksum = calc_checksum(b, 43);
        if (packet.check != reflect4(checksum & 0xF) ||
                packet.parity != (checksum >> 4)) {
            return DECODE_FAIL_MIC;
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",                DATA_STRING, "Funkbus-Remote",
                "id",           "Serial number",   DATA_INT, packet.sn,
                "battery_ok",   "Battery",         DATA_INT, packet.bat ? 0 : 1,
                "command",      "Switch",          DATA_INT, packet.command,
                "group",        "Group",           DATA_INT, packet.group,
                "action",       "Action",          DATA_INT, packet.action,
                "repeat",       "Repeat",          DATA_INT, packet.repeat,
                "longpress",    "Longpress",       DATA_INT, packet.longpress,
                "mic",          "Integrity",       DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        events++;
    }

    return events;
}

static char *output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "command",
        "group",
        "action",
        "repeat",
        "longpress",
        "mic",
        NULL,
};

r_device funkbus_remote = {
        .name        = "Funkbus / Instafunk (Berker, Jira, Jung)",
        .modulation  = OOK_PULSE_DMC,
        .short_width = 500,
        .long_width  = 1000,
        .reset_limit = 2000,
        .gap_limit   = 1500,
        .sync_width  = 4000,
        .tolerance   = 300, // us
        .decode_fn   = &funkbus_decode,
        .fields      = output_fields,
};
