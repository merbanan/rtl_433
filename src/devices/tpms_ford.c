/* FSK 8 byte Manchester encoded TPMS with simple checksum.
 * Seen on Ford Fiesta, Focus, ...
 *
 * Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Packet nibbles:  IIIIIIII PP TT FF CC
 * I = ID
 * P = likely Pressure
 * T = likely Temperature
 * F = Flags, (46: 87% 1e: 5% 06: 2% 4b: 1% 66: 1% 0e: 1% 44: 1%)
 * C = Checksum, SUM bytes 0 to 6 = byte 7
*/

#include "decoder.h"

// full preamble is 55 55 55 56 (inverted: aa aa aa a9)
static const uint8_t preamble_pattern[2] = { 0xaa, 0xa9 }; // 16 bits

static int tpms_ford_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    data_t *data;
    unsigned int start_pos;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    unsigned id;
    char id_str[9];
    int code;
    char code_str[7];

    start_pos = bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 160);
    // require 64 data bits
    if (start_pos-bitpos < 128) {
        return 0;
    }
    b = packet_bits.bb[0];

    if (((b[0]+b[1]+b[2]+b[3]+b[4]+b[5]+b[6]) & 0xff) != b[7]) {
        return 0;
    }

    id = (unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    sprintf(id_str, "%08x", id);

    code = b[4]<<16 | b[5]<<8 | b[6];
    sprintf(code_str, "%06x", code);

    data = data_make(
        "model",        "",     DATA_STRING, "Ford",
        "type",         "",     DATA_STRING, "TPMS",
        "id",           "",     DATA_STRING, id_str,
        "code",         "",     DATA_STRING, code_str,
        "mic",          "",     DATA_STRING, "CHECKSUM",
        NULL);

    decoder_output_data(decoder, data);
    return 1;
}

static int tpms_ford_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    int row;
    unsigned bitpos;
    int events = 0;

    bitbuffer_invert(bitbuffer);

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                (const uint8_t *)&preamble_pattern, 16)) + 144 <=
                bitbuffer->bits_per_row[row]) {
            events += tpms_ford_decode(decoder, bitbuffer, row, bitpos + 16);
            bitpos += 15;
        }
    }

    return events;
}

static char *output_fields[] = {
    "model",
    "type",
    "id",
    "code",
    "mic",
    NULL
};

r_device tpms_ford = {
    .name           = "Ford TPMS",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 52, // 12-13 samples @250k
    .long_width     = 52, // FSK
    .reset_limit    = 150, // Maximum gap size before End Of Message [us].
    .decode_fn      = &tpms_ford_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
