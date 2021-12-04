/** @file funkbus.c
 *  @author Markus Sattler
 *  @date 2021-12-04
 *
 *  @brief Funkbus / Instafunk
 *  used by Berker, Jira, Jung and may more
 *  developed by Insta GmbH
 *
 *  @copyright Copyright (C) 2021 Markus Sattler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

/*
    frequency: 433.92Mhz
    preamble: 4000us
    short: 500us
    long: 1000us
    encoding: Differential Manchester Biphase–Mark (BP-M)

      __ __       __    __ __    __
     |     |     |  |  |     |  |  |
    _|     |__ __|  |__|     |__|  |__.....
     |  0  |  0  | 	1  |  0  |  1  |

    mic: parity + unknown 4bit checksum
    bits: 48
    structure: see funkbus_packet_t


    some details can be found by searching
    "instafunk RX/TX-Modul pdf"

*/

#include "decoder.h"

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
    uint8_t bat : 1; // battery low
    uint8_t r2 : 1;  // unknown

    uint8_t sw : 3; // button on the remote
    uint8_t ch : 2; // remote channel 0-2 are switches 3 == light scene
    uint8_t r3 : 1; // unknown

    funkbus_action_t action : 2;
    uint8_t repeat : 1; // 1 == not first send of packet
    uint8_t global : 1; // 1 == global ON / OFF button
    uint8_t parity : 1; // parity over all bits
    uint8_t check : 4;  // how to calculate is unknown
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

static uint8_t calc_parity(uint8_t const *bitrow, size_t len)
{
    uint8_t result = 0;
    for (uint8_t i = 0; i < len; i++) {
        if (bitrow_get_bit(bitrow, i)) {
            result++;
        }
    }
    return result & 0x01;
}

static int funkbus_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{

    for (uint16_t row = 0; row < bitbuffer->num_rows; row++) {
        uint16_t len = bitbuffer->bits_per_row[row];
        uint8_t *bin  = bitbuffer->bb[row];

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

        // remote
        if (packet.typ != 0x4 || packet.subtyp != 0x3) {
            return DECODE_ABORT_EARLY;
        }

        packet.sn     = get(20);
        packet.r1     = get(2);
        packet.bat    = get(1);
        packet.r2     = get(2);
        packet.sw     = get(3);
        packet.ch     = get(2);
        packet.r3     = get(1);
        packet.action = get(2);
        packet.repeat = get(1);
        packet.global = get(1);
        packet.parity = get(1);
        packet.check  = get(4); // how to calc?

        if (packet.parity != calc_parity(bin, 43)) {
            return DECODE_FAIL_MIC;
        }

        // calc id based on sn, ch + sw
        uint64_t id = packet.sw + (packet.ch << 4) + (packet.sn << 8);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",                DATA_STRING, "funkbus",
                "typ",          "",                DATA_STRING, "remote",
                "id",           "id",              DATA_INT, id,
                "sn",           "serial number",   DATA_INT, packet.sn,
                "bat",          "battery empty",   DATA_INT, packet.bat,
                "sw",           "switch",          DATA_INT, packet.sw,
                "ch",           "channel",         DATA_INT, packet.ch,
                "action",       "action",          DATA_INT, packet.action,
                "repeat",       "repeat",          DATA_INT, packet.repeat,
                "global",       "global",          DATA_INT, packet.global,
#ifdef DEBUG
                "parity",       "parity",          DATA_INT, packet.parity,
                "check",        "check",           DATA_INT, packet.check,
#endif
                "mic",          "integrity",       DATA_STRING, "PARITY",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
    }

#ifdef DEBUG
    bitbuffer_debug(bitbuffer);
#endif

    return 1;
}

static char *output_fields[] = {
        "model",
        "typ",
        "id",
        "sn",
        "bat",
        "sw",
        "ch",
        "action",
        "repeat",
        "global",
#ifdef DEBUG
        "parity",
        "check",
#endif
        "mic",
        NULL,
};

r_device funkbus_remote = {
        .name        = "Funkbus remote",
        .modulation  = OOK_PULSE_DMC,
        .short_width = 500,
        .long_width  = 1000,
        .reset_limit = 2000,
        .gap_limit   = 1500,
        .sync_width  = 4000,
        .tolerance   = 300, // us
        .decode_fn   = &funkbus_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
