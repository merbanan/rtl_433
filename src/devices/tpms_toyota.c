/* FSK 9-byte Manchester encoded TPMS data with CRC-7.
 * Seen on a Toyota Auris(Corolla). The manufacturers of the Toyota TPMS are
 * Pacific Industrial Corp and sometimes TRW Automotive and might also be used
 * in other car brands. Contact me with your observations!
 *
 * Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * There are 15 bits of sync followed by 72 bits manchester encoded data.
 * Flipping the bits gives a CRC init of 0, which seems more likely.
 * E.g. 10101 0101 1000 01 100110011010[...60 manchester bits]01011001101000
 * The first data bit is always one, last always 0. Dropping the first data
 * bit (which could reasonably be part of the sync) we get 72 bits: 8 bytes
 * payload and 1 byte CRC (7-bit CRC in the MSBs with the LSB set to 0).
 *
 * Particularly interesting is the use of a CRC-7 with 0x7d as truncated poly.
 *
 * The first 4 bytes are the ID. Followed by likely two 8-bit values,
 * a 16-bit value and then the CRC. The values seem to be signed.
 */

#include "rtl_433.h"
#include "util.h"

// full preamble is 1 0101 0101 1000 01 = 55 8
static const unsigned char preamble_pattern[2] = { 0x55, 0x80 }; // 12 bits

static int tpms_toyota_decode(bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos) {
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    unsigned int start_pos;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    unsigned id;
    char id_str[9];
    unsigned code;
    char code_str[21];
    int crc;

    // skip the first 1 bit, i.e. raw "01" to get 72 bits
    start_pos = bitbuffer_manchester_decode(bitbuffer, row, bitpos + 2, &packet_bits, 80);
    if (start_pos-bitpos < 144) {
        return 0;
    }
    b = packet_bits.bb[0];

    crc = b[8] >> 1;
    if (crc7(b, 8, 0x7d, 0x00) != crc) {
        return 0;
    }

    id = b[0]<<24 | b[1]<<16 | b[2]<<8 | b[3];
    sprintf(id_str, "%08x", id);

    code = b[4]<<24 | b[5]<<16 | b[6]<<8 | b[7];
    sprintf(code_str, "%08x", code);

    local_time_str(0, time_str);
    data = data_make(
        "time",         "",     DATA_STRING, time_str,
        "model",        "",     DATA_STRING, "Toyota",
        "type",         "",     DATA_STRING, "TPMS",
        "id",           "",     DATA_STRING, id_str,
        "code",         "",     DATA_STRING, code_str,
        "mic",          "",     DATA_STRING, "CRC",
        NULL);

    data_acquired_handler(data);
    return 1;
}

static int tpms_toyota_callback(bitbuffer_t *bitbuffer) {
    unsigned bitpos = 0;
    int events = 0;

    bitbuffer_invert(bitbuffer);

    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, (const uint8_t *)&preamble_pattern, 12)) + 158 <=
            bitbuffer->bits_per_row[0]) {
        events += tpms_toyota_decode(bitbuffer, 0, bitpos + 12);
        bitpos += 2;
    }

    return events;
}

static char *output_fields[] = {
    "time",
    "model",
    "type",
    "id",
    "code",
    "mic",
    NULL
};

r_device tpms_toyota = {
    .name           = "Toyota TPMS",
    .modulation     = FSK_PULSE_PCM,
    .short_limit    = 52, // 12-13 samples @250k
    .long_limit     = 52, // FSK
    .reset_limit    = 150, // Maximum gap size before End Of Message [us].
    .json_callback  = &tpms_toyota_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields,
};
