/** @file
    Unbranded aftermarket TPMS sensor using CMT2220LY receiver module at 433.92 MHz.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
 Gear Hive / Unbranded aftermarket TPMS sensor.

The sensor operates at 433.92 MHz using OOK modulation with Manchester encoding
(MC_ZEROBIT). The original receiver module is based on the CMT2220LY chip.

The Manchester-decoded bitstream (NRZ) consists of:
- Preamble: long run of zeros
- Sync word: 0x2594 (16 bits)
- Payload: 9 bytes differentially XOR encoded
- Trailing: 2 padding bits

Differential XOR decode (seeded from sync byte 0x94):
    p[0] = b[0] XOR 0x94
    p[i] = b[i] XOR b[i-1], for i = 1..8

Data layout (nibbles):

    CC CS II II II PP TT KK

- C: 12 bit rolling counter (p[0] + p[1] high nibble)
- S: 4 bit sensor class (p[1] low nibble)
- I: 24 bit sensor ID (p[2..4])
- P: 8 bit pressure raw (p[5])
- T: 8 bit temperature (p[6] bits 1-0, p[7] bits 7-6)

    p[6] bits 5-2 = 0x08, p[7] bits 5-0 = 0x35 (fixed flags)

- K: 8 bit unknown / checksum (p[8])

Pressure:
    base = (80 + (p[1] & 0x0f) * 64) & 0xff
    pressure_kPa = ((p[5] - base + 256) & 0xff) * 6.25

Temperature:
    temp_C = (p[7] >> 6) + (p[6] & 0x03) * 4 + 21

Sanity checks:
    (p[6] & 0x3c) == 0x20
    (p[7] & 0x3f) == 0x35

Flex decoder equivalent:
    rtl_433 -X 'n=name,m=OOK_MC_ZEROBIT,s=120,l=224,r=800,g=0,t=0,y=612,bits>=160'
*/

static int tmps_gear_hive_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    unsigned payload_start = bitpos + 16;
    unsigned remaining_bits = bitbuffer->bits_per_row[row] - payload_start;

    if (remaining_bits < 72) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t raw[9] = {0};
    bitbuffer_extract_bytes(bitbuffer, row, payload_start, raw, 72);

    // Differential XOR decode, seeded from sync byte 0x94
    uint8_t p[9];
    p[0] = raw[0] ^ 0x94;
    for (int i = 1; i < 9; i++) {
        p[i] = raw[i] ^ raw[i - 1];
    }

    // Sanity checks
    if ((p[6] & 0x3c) != 0x20) {
        return DECODE_FAIL_SANITY;
    }
    if ((p[7] & 0x3f) != 0x35) {
        return DECODE_FAIL_SANITY;
    }

    int sensor_class = p[1] & 0x0f;
    int counter = ((p[1] >> 4) << 8) | p[0];
    uint32_t id = ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 8) | p[4];

    int pressure_raw = p[5];
    int base = (80 + sensor_class * 64) & 0xff;
    float pressure_kpa = (float)((pressure_raw - base + 256) & 0xff) * 6.25f;

    int temp_bits = (p[7] >> 6) | ((p[6] & 0x03) << 2);
    float temp_c = (float)temp_bits + 21.0f;

    int unknown = p[8];

    char id_str[7];
    snprintf(id_str, sizeof(id_str), "%06x", id);

    /* clang-format off */
    data_t *data_output = data_make(
            "model",         "Model",         DATA_STRING, "Gear-Hive",
            "type",          "Type",          DATA_STRING, "TPMS",
            "id",            "ID",            DATA_STRING, id_str,
            "counter",       "Counter",       DATA_INT,    counter,
            "pressure_kPa",  "Pressure",      DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure_kpa,
            "temperature_C", "Temperature",   DATA_FORMAT, "%.0f C",   DATA_DOUBLE, (double)temp_c,
            "mic",           "Integrity",     DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data_output);
    return 1;
}

/** @sa tmps_gear_hive_decode() */
static int tmps_gear_hive_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const sync_pattern[] = {0x25, 0x94}; // 16 bits

    int ret    = 0;
    int events = 0;

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos, sync_pattern, 16)) + 16 + 72
                <= bitbuffer->bits_per_row[row]) {
            ret = tmps_gear_hive_decode(decoder, bitbuffer, row, bitpos);
            if (ret > 0)
                events += ret;
            bitpos += 16;
        }
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "counter",
        "pressure_kPa",
        "temperature_C",
        "mic",
        NULL,
};

r_device const tmps_gear_hive = {
        .name        = "Gear Hive TPMS sensor",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 120,
        .long_width  = 224,
        .gap_limit   = 0,
        .reset_limit = 800,
        .decode_fn   = &tmps_gear_hive_callback,
        .fields      = output_fields,
};
