/* FSK 8-byte Differential Manchester encoded TPMS data with CRC-8.
 * Pacific PMV-107J TPMS (315MHz) sensors used by Toyota
 * based on work by Werner Johansson.
 *
 * Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * 66 bits Differential Manchester encoded TPMS data with CRC-8.
 * II II II I F* PP NN TT CC
 * I: ID (28 bit)
 * F*: Flags, 6 bits (BCC00F, battery_low, repeat_counter, failed)
 * P: Tire pressure (PSI/0.363 + 40 or kPa/2.48 + 40)
 * N: Inverted tire pressure
 * T: Tire temperature (Celsius +40, range from -40 to +215 C)
 * C: CRC over bits 0 - 57, poly 0x13, init 0
 */

#include "decoder.h"

// full preamble is (7 bits) 11111 10
static const unsigned char preamble_pattern[1] = {0xf8}; // 6 bits

static int tpms_pmv107j_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    data_t *data;
    unsigned int start_pos;
    bitbuffer_t packet_bits = {0};
    uint8_t b[9];
    unsigned id;
    char id_str[9];
    unsigned status, pressure1, pressure2, temp, battery_low, counter, failed;
    float pressure_kpa, temperature_c;
    int crc;

    start_pos = bitbuffer_differential_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 70); // 67 bits expected
    if (start_pos - bitpos < 67*2) {
        return 0;
    }
    if (decoder->verbose > 1)
        bitbuffer_print(&packet_bits);

    // realign the buffer, prepending 6 bits of 0.
    b[0] = packet_bits.bb[0][0] >> 6;
    bitbuffer_extract_bytes(&packet_bits, 0, 2, b + 1, 64);
    if (decoder->verbose > 1) {
        fprintf(stderr, "Realigned: ");
        bitrow_print(b, 72);
    }

    crc = b[8];
    if (crc8(b, 8, 0x13, 0x00) != crc) {
        return 0;
    }

    id = b[0] << 26| b[1] << 18 | b[2] << 10 | b[3] << 2 | b[4] >> 6; // realigned bits 6 - 34
    status = b[4] & 0x3f; // status bits and 0 filler
    battery_low = (b[4] & 0x20) >> 5;
    counter = (b[4] & 0x18) >> 3;
    failed = b[4] & 0x01;
    pressure1 = b[5];
    pressure2 = b[6] ^ 0xff;
    temp = b[7];
    pressure_kpa = (pressure1 - 40.0) * 2.48;
    temperature_c = temp - 40.0;

    if (pressure1 != pressure2) {
        if (decoder->verbose)
            fprintf(stderr, "Toyota TPMS pressure check error: %02x vs %02x\n", pressure1, pressure2);
        return 0;
    }

    sprintf(id_str, "%08x", id);

    data = data_make(
        "model",            "",     DATA_STRING,    "PMV-107J",
        "type",             "",     DATA_STRING,    "TPMS",
        "id",               "",     DATA_STRING,    id_str,
        "status",           "",     DATA_INT,       status,
        "battery",          "",     DATA_STRING,    battery_low ? "LOW" : "OK",
        "counter",          "",     DATA_INT,       counter,
        "failed",           "",     DATA_STRING,    failed ? "FAIL" : "OK",
        "pressure_kPa",     "",     DATA_DOUBLE,    pressure_kpa,
        "temperature_C",    "",     DATA_DOUBLE,    temperature_c,
        "mic",              "",     DATA_STRING,    "CRC",
        NULL);

    decoder_output_data(decoder, data);
    return 1;
}

static int tpms_pmv107j_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    unsigned bitpos = 0;
    int events = 0;

    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, (const uint8_t *)&preamble_pattern, 6)) + 67*2 <=
            bitbuffer->bits_per_row[0]) {
        events += tpms_pmv107j_decode(decoder, bitbuffer, 0, bitpos + 6);
        bitpos += 2;
    }

    return events;
}

static char *output_fields[] = {
    "model",
    "type",
    "id",
    "status",
    "battery",
    "counter",
    "failed",
    "pressure_kPa",
    "temperature_C",
    "mic",
    NULL
};

r_device tpms_pmv107j = {
    .name           = "PMV-107J (Toyota) TPMS",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 100, // 25 samples @250k
    .long_width     = 100, // FSK
    .reset_limit    = 250, // Maximum gap size before End Of Message [us].
    .decode_fn      = &tpms_pmv107j_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
