/* FSK 9-byte Differential Manchester encoded TPMS data with CRC-8.
 * Pacific Industries Co.Ltd. PMV-C210
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
 * There are 14 bits sync followed by 72 bits manchester encoded data and
 * 3 bits trailer.
 * E.g. 01010101001111 00110011 [...64 manchester bits] 00101010111
 *
 * The first 4 bytes are the ID. Followed by 1-bit state,
 * 8-bit values of pressure, temperature, 7-bit state, 8-bit inverted pressure
 * and then the a CRC-8 with 0x07 truncated poly and init 0x80.
 * The temperature is offset by 40 deg C.
 * The pressure seems to be 1/4 PSI offset by -7 PSI (i.e. 28 raw = 0 PSI).
 */

#include "rtl_433.h"
#include "util.h"

// full preamble is 0101 0101 0011 11 = 55 3c
static const unsigned char preamble_pattern[2] = {0x54, 0xf0}; // 12 bits

static int tpms_toyota_decode(bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos) {
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    unsigned int start_pos;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    unsigned id;
    char id_str[9];
    unsigned status, pressure1, pressure2, temp;
    int crc;

    // skip the first 1 bit, i.e. raw "01" to get 72 bits
    start_pos = bitbuffer_differential_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 80);
    if (start_pos - bitpos < 144) {
        return 0;
    }
    b = packet_bits.bb[0];

    crc = b[8];
    if (crc8(b, 8, 0x07, 0x80) != crc) {
        return 0;
    }

    id = b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    status = (b[4] & 0x80) | (b[6] & 0x7f); // status bit and 0 filler
    pressure1 = (b[4] & 0x7f) << 1 | b[5] >> 7;
    temp = (b[5] & 0x7f) << 1 | b[6] >> 7;
    pressure2 = b[7] ^ 0xff;

    if (pressure1 != pressure2) {
        if (debug_output)
            fprintf(stderr, "Toyota TPMS pressure check error: %02x vs %02x\n", pressure1, pressure2);
        return 0;
    }

    sprintf(id_str, "%08x", id);

    local_time_str(0, time_str);
    data = data_make(
        "time",             "",     DATA_STRING,    time_str,
        "model",            "",     DATA_STRING,    "Toyota",
        "type",             "",     DATA_STRING,    "TPMS",
        "id",               "",     DATA_STRING,    id_str,
        "status",           "",     DATA_INT,       status,
        "pressure_PSI",     "",     DATA_DOUBLE,    pressure1*0.25-7.0,
        "temperature_C",    "",     DATA_DOUBLE,    temp-40.0,
        "mic",              "",     DATA_STRING,    "CRC",
        NULL);

    data_acquired_handler(data);
    return 1;
}

static int tpms_toyota_callback(bitbuffer_t *bitbuffer) {
    unsigned bitpos = 0;
    int events = 0;

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
    "status",
    "pressure_PSI",
    "temperature_C",
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
