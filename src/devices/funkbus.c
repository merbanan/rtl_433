/** @file
    Funkbus / Instafunk
    used by Berker, Jira, Jung and may more
    developed by Insta GmbH

    Copyright (C) 2021 Markus Sattler

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 */

/**
    frequency: 433.92Mhz
    preamble: 4000us
    short: 500us
    long: 1000us
    encoding: Differential Manchester Biphase–Mark (BP-M)

      __ __       __    __ __    __
     |     |     |  |  |     |  |  |
    _|     |__ __|  |__|     |__|  |__.....
     |  0  |  0  | 	1  |  0  |  1  |

    mic: parity + lfsr with 8bit mask 0x8C sifted left by 2 bit
    bits: 48
    structure: see funkbus_packet_t
    endian: LSB


    some details can be found by searching
    "instafunk RX/TX-Modul pdf"

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
    uint8_t r2 : 1;  // unknown

    uint8_t sw : 3;    // button on the remote
    uint8_t group : 2; // remote channel group 0-2 (A-C) are switches, 3 == light scene
    uint8_t r3 : 1;    // unknown

    funkbus_action_t action : 2;
    uint8_t repeat : 1;    // 1 == not first send of packet
    uint8_t longpress : 1; // longpress of button for (dim up/down, scene lerning)
    uint8_t parity : 1;    // parity over all bits before
    uint8_t check : 4;     // lfsr with 8bit mask 0x8C sifted left by 2 bit
} __attribute__((packed)) funkbus_packet_t;

static uint64_t get_data_lsb(uint8_t const *bitrow, size_t start, uint8_t end)
{
    uint64_t result = 0;
    uint64_t mask   = 1;
    result          = 0;
    for (; start <= end; mask <<= 1)
        if (bitrow_get_bit(bitrow, start++) != 0)
            result |= mask;
    return result;
}

static uint8_t calc_checksum(uint8_t const *bitrow, size_t len)
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

    for (uint16_t row = 0; row < bitbuffer->num_rows; row++) {
        const uint16_t len = bitbuffer->bits_per_row[row];
        const uint8_t *bin = bitbuffer->bb[row];

        if (len < 48) {
            return DECODE_ABORT_LENGTH;
        }

        uint8_t get_c = 0;
#define get(x) \
    get_data_lsb(bin, get_c, get_c + x - 1); \
    get_c += x

        funkbus_packet_t packet;
        packet.typ    = get(4);
        packet.subtyp = get(4);

        // only handle packet typ for remotes remote
        if (packet.typ != 0x4 || packet.subtyp != 0x3) {
            return DECODE_ABORT_EARLY;
        }

        packet.sn        = get(20);
        packet.r1        = get(2);
        packet.bat       = get(1);
        packet.r2        = get(2);
        packet.sw        = get(3);
        packet.group     = get(2);
        packet.r3        = get(1);
        packet.action    = get(2);
        packet.repeat    = get(1);
        packet.longpress = get(1);
        packet.parity    = get(1);
        packet.check     = get(4);

        uint8_t checksum = calc_checksum(bin, 43);
        if (packet.check != reflect4(checksum & 0xF) ||
                packet.parity != (checksum >> 4)) {
            return DECODE_FAIL_MIC;
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",                DATA_STRING, "Funkbus-Remote",
                "id",           "Serial number",   DATA_INT, packet.sn,
                "battery_ok",   "Battery",         DATA_INT, packet.bat ? 0 : 1,
                "sw",           "Switch",          DATA_INT, packet.sw,
                "group",        "Group",           DATA_INT, packet.group,
                "channel",      "Channel",         DATA_INT, ((packet.group << 3) + packet.sw),
                "action",       "Action",          DATA_INT, packet.action,
                "repeat",       "Repeat",          DATA_INT, packet.repeat,
                "longpress",    "Longpress",       DATA_INT, packet.longpress,
                "mic",          "Integrity",       DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
    }

    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "sw",
        "group",
        "channel",
        "action",
        "repeat",
        "global",
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
        .disabled    = 0,
        .fields      = output_fields,
};
