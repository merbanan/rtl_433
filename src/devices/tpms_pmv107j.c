/** @file
    FSK 8-byte Differential Manchester encoded TPMS data with CRC-8.

    Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
FSK 8-byte Differential Manchester encoded TPMS data with CRC-8.
Pacific PMV-107J TPMS (315MHz) sensors used by Toyota
based on work by Werner Johansson.

66 bits Differential Manchester encoded TPMS data with CRC-8.

    II II II I F* PP NN TT CC

- I: ID (28 bit)
- F*: Flags, 6 bits (BCC00F, battery_low, repeat_counter, failed)
- P: Tire pressure (PSI/0.363 + 40 or kPa/2.48 + 40)
- N: Inverted tire pressure
- T: Tire temperature (Celsius +40, range from -40 to +215 C)
- C: CRC over bits 0 - 57, poly 0x13, init 0
*/

#include "decoder.h"

static int tpms_pmv107j_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    uint8_t b[9];

    unsigned start_pos = bitbuffer_differential_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 70); // 67 bits expected
    if (start_pos - bitpos < 67 * 2) {
        return 0;
    }
    decoder_log_bitbuffer(decoder, 2, __func__, &packet_bits, "");

    // realign the buffer, prepending 6 bits of 0.
    b[0] = packet_bits.bb[0][0] >> 6;
    bitbuffer_extract_bytes(&packet_bits, 0, 2, b + 1, 64);
    decoder_log_bitrow(decoder, 2, __func__, b, 72, "Realigned");

    int crc = b[8];
    if (crc8(b, 8, 0x13, 0x00) != crc) {
        return 0;
    }

    unsigned id            = b[0] << 26 | b[1] << 18 | b[2] << 10 | b[3] << 2 | b[4] >> 6; // realigned bits 6 - 34
    unsigned status        = b[4] & 0x3f;                                                  // status bits and 0 filler
    unsigned battery_low   = (b[4] & 0x20) >> 5;
    unsigned counter       = (b[4] & 0x18) >> 3;
    unsigned failed        = b[4] & 0x01;
    unsigned pressure1     = b[5];
    unsigned pressure2     = b[6] ^ 0xff;
    unsigned temp          = b[7];
    float pressure_kpa  = (pressure1 - 40.0f) * 2.48f;
    float temperature_c = temp - 40.0f;

    if (pressure1 != pressure2) {
        decoder_logf(decoder, 1, __func__, "Toyota TPMS pressure check error: %02x vs %02x", pressure1, pressure2);
        return 0;
    }

    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%08x", id);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING,    "PMV-107J",
            "type",             "",             DATA_STRING,    "TPMS",
            "id",               "",             DATA_STRING,    id_str,
            "status",           "",             DATA_INT,       status,
            "battery_ok",       "",             DATA_INT,       !battery_low,
            "counter",          "",             DATA_INT,       counter,
            "failed",           "",             DATA_STRING,    failed ? "FAIL" : "OK",
            "pressure_kPa",     "",             DATA_DOUBLE,    pressure_kpa,
            "temperature_C",    "",             DATA_DOUBLE,    temperature_c,
            "mic",              "Integrity",    DATA_STRING,    "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_pmv107j_decode() */
static int tpms_pmv107j_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is (7 bits) 11111 10
    uint8_t const preamble_pattern[1] = {0xf8}; // 6 bits

    unsigned bitpos = 0;
    int ret         = 0;
    int events      = 0;

    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 6)) + 67 * 2 <=
            bitbuffer->bits_per_row[0]) {
        ret = tpms_pmv107j_decode(decoder, bitbuffer, 0, bitpos + 6);
        if (ret > 0)
            events += ret;
        bitpos += 2;
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "status",
        "battery_ok",
        "counter",
        "failed",
        "pressure_kPa",
        "temperature_C",
        "mic",
        NULL,
};

r_device const tpms_pmv107j = {
        .name        = "PMV-107J (Toyota) TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100, // 25 samples @250k
        .long_width  = 100, // FSK
        .reset_limit = 250, // Maximum gap size before End Of Message [us].
        .decode_fn   = &tpms_pmv107j_callback,
        .fields      = output_fields,
};
