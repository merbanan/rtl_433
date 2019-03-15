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
 * Packet nibbles:  F F/P PP TT II II II ?? ?? CC
 * F = flags, (seen: c0: 22% c8: 14% d0: 31% d8: 33%) maybe 110??T
 * P = Pressure, 10 bit 0.75 kPa
 * I = id, 24-bit little-endian
 * T = Temperature in C, offset -30
 * ? = Unknown, mostly 0xffff
 * C = Checksum, CRC-8 truncated poly 0x07 init 0x00
 */

#include "decoder.h"

// full preamble is 55 55 55 56 (inverted: aa aa aa a9)
static const uint8_t preamble_pattern[2] = { 0xaa, 0xa9 }; // 16 bits

static int tpms_renault_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    data_t *data;
    unsigned int start_pos;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    int flags;
    char flags_str[3];
    unsigned id;
    char id_str[7];
    int pressure_raw, temp_c, unknown;
    double pressure_kpa;
    char code_str[5];

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

    flags = b[0] >> 2;
    sprintf(flags_str, "%02x", flags);

    id = b[5]<<16 | b[4]<<8 | b[3]; // little-endian
    sprintf(id_str, "%06x", id);

    pressure_raw = (b[0] & 0x03) << 8 | b[1];
    pressure_kpa = pressure_raw * 0.75;
    temp_c       = b[2] - 30;
    unknown      = b[7] << 8 | b[6]; // little-endian, fixed 0xffff?
    sprintf(code_str, "%04x", unknown);

    data = data_make(
            "model",            "", DATA_STRING, "Renault",
            "type",             "", DATA_STRING, "TPMS",
            "id",               "", DATA_STRING, id_str,
            "flags",            "", DATA_STRING, flags_str,
            "pressure_kPa",     "", DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure_kpa,
            "temperature_C",    "", DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temp_c,
            "mic",              "", DATA_STRING, "CRC",
            NULL);

    decoder_output_data(decoder, data);
    return 1;
}

static int tpms_renault_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
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
            events += tpms_renault_decode(decoder, bitbuffer, row, bitpos + 16);
            bitpos += 15;
        }
    }

    return events;
}

static char *output_fields[] = {
    "model",
    "type",
    "id",
    "flags",
    "pressure_kPa",
    "temperature_C",
    "mic",
    NULL
};

r_device tpms_renault = {
    .name           = "Renault TPMS",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 52, // 12-13 samples @250k
    .long_width     = 52, // FSK
    .reset_limit    = 150, // Maximum gap size before End Of Message [us].
    .decode_fn      = &tpms_renault_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
