/* FSK 9 byte Manchester encoded TPMS with CRC.
 * Seen on Renault Clio, Renault Captur and maybe Dacia Sandero.
 *
 * Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Packet nibbles:  FF PP PP II II II TT TT CC
 * F = flags, (seen: c1: 22% c8: 4% c9: 10% d0: 2% d1: 29% d8: 4% d9: 29%)
 * P = Pressure, 16-bit little-endian
 * I = id, 24-bit little-endian
 * T = Unknown, likely Temperature, 16-bit little-endian
 * C = Checksum, CRC-8 truncated poly 0x07 init 0x00
 */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

// full preamble is 55 55 55 56 (inverted: aa aa aa a9)
static const uint8_t preamble_pattern[2] = { 0xaa, 0xa9 }; // 16 bits

static int tpms_renault_decode(bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos) {
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    unsigned int start_pos;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    int flags;
    char flags_str[3];
    unsigned id;
    char id_str[7];
    int maybe_pressure, maybe_temp;
    char code_str[10];

    start_pos = bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 160);
    // require 72 data bits
    if (start_pos-bitpos < 144) {
        return 0;
    }
    b = packet_bits.bb[0];

    // 0x83; 0x107 FOP-8; ATM-8; CRC-8P
    if (crc8(b, 8, 0x07, 0x00) != b[8]) {
        return 0;
    }

    flags = b[0];
    sprintf(flags_str, "%02x", flags);

    id = b[5]<<16 | b[4]<<8 | b[3]; // little-endian
    sprintf(id_str, "%06x", id);

    maybe_pressure = b[2]<<8 | b[1]; // little-endian
    maybe_temp = b[7]<<8 | b[6]; // little-endian
    sprintf(code_str, "%04x %04x", maybe_pressure, maybe_temp);

    local_time_str(0, time_str);
    data = data_make(
        "time",         "",     DATA_STRING, time_str,
        "model",        "",     DATA_STRING, "Renault",
        "type",         "",     DATA_STRING, "TPMS",
        "id",           "",     DATA_STRING, id_str,
        "flags",        "",     DATA_STRING, flags_str,
        "code",         "",     DATA_STRING, code_str,
        "mic",          "",     DATA_STRING, "CRC",
        NULL);

    data_acquired_handler(data);
    return 1;
}

static int tpms_renault_callback(bitbuffer_t *bitbuffer) {
    int row;
    unsigned bitpos;
    int events = 0;

    bitbuffer_invert(bitbuffer);

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                (const uint8_t *)&preamble_pattern, 16)) + 160 <=
                bitbuffer->bits_per_row[row]) {
            events += tpms_renault_decode(bitbuffer, row, bitpos + 16);
            bitpos += 15;
        }
    }

    return events;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "flags",
    "code",
    "mic",
    NULL
};

r_device tpms_renault = {
    .name           = "Renault TPMS",
    .modulation     = FSK_PULSE_PCM,
    .short_limit    = 52, // 12-13 samples @250k
    .long_limit     = 52, // FSK
    .reset_limit    = 150, // Maximum gap size before End Of Message [us].
    .json_callback  = &tpms_renault_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields,
};
