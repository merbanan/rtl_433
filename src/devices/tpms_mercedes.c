/** @file
    Mercedes-Benz Sprinter TPMS.

    Copyright (C) 2026 Simone Romeo

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Mercedes-Benz Sprinter TPMS.

Seen on a 2025 Mercedes-Benz Sprinter 4500 factory TPMS sensor.

The first known captures decode through a 52 us FSK PCM path. Later cleaner
captures show the same payload behind a 25 us inverted Manchester path after a
0x55a6 preamble. Keep both demodulator entries so either capture quality works.

Data layout:

    II II II II II PP UU NN FF CC

- I: 40 bit sensor ID
- P: 8 bit pressure, 2.5 kPa units
- U: 8 bit unknown
- N: 8 bit rolling counter
- F: 8 bit flags/status, observed as 0x6b in the first captures
- C: CRC-8, poly 0x2f, init 0xaa

More captures with known tire positions, pressures, and temperatures are needed
to confirm the unknown byte and all possible flag values. The pressure scaling
matches the later reported driver-side front and rear tire pressure samples.
*/

#include "decoder.h"
#include "r_util.h"

static int tpms_mercedes_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t b[10] = {0};
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, 80);

    if (crc8(b, 9, 0x2f, 0xaa) != b[9]) {
        return DECODE_FAIL_MIC;
    }

    int pressure_raw = b[5];
    int unknown      = b[6];
    int counter      = b[7];
    int flags        = b[8];
    double pressure_kpa = pressure_raw * 2.5;
    double pressure_psi = kpa2psi((float)pressure_kpa);

    // Keep the provisional flag filter while more sample sets are gathered.
    if (pressure_psi < 10.0 || pressure_psi > 150.0 || flags != 0x6b) {
        return DECODE_ABORT_EARLY;
    }

    char id_str[5 * 2 + 1];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4]);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",            DATA_STRING, "Mercedes-Sprinter",
            "type",             "",            DATA_STRING, "TPMS",
            "id",               "",            DATA_STRING, id_str,
            "pressure_kPa",     "Pressure",    DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, pressure_kpa,
            "pressure_PSI",     "Pressure",    DATA_FORMAT, "%.1f PSI", DATA_DOUBLE, pressure_psi,
            "unknown",          "",            DATA_FORMAT, "0x%02x",   DATA_INT,    unknown,
            "counter",          "",            DATA_INT,    counter,
            "flags",            "",            DATA_FORMAT, "0x%02x",   DATA_INT,    flags,
            "mic",              "Integrity",   DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static int tpms_mercedes_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int events = 0;
    int ret    = 0;

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        for (unsigned bitpos = 0; bitpos + 80 <= bitbuffer->bits_per_row[row]; ++bitpos) {
            ret = tpms_mercedes_decode(decoder, bitbuffer, row, bitpos);
            if (ret > 0) {
                events += ret;
                bitpos += 79;
            }
        }
    }

    return events > 0 ? events : ret;
}

static int tpms_mercedes_mc_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0x55, 0xa6};
    bitbuffer_t inverted            = *bitbuffer;
    int events                      = 0;
    int ret                         = 0;

    bitbuffer_invert(&inverted);

    for (int row = 0; row < inverted.num_rows; ++row) {
        unsigned bitpos = 0;
        while ((bitpos = bitbuffer_search(&inverted, row, bitpos, preamble_pattern, sizeof(preamble_pattern) * 8))
                        + sizeof(preamble_pattern) * 8 + 160 <=
                inverted.bits_per_row[row]) {
            bitbuffer_t decoded = {0};
            bitbuffer_manchester_decode(&inverted, row, bitpos + sizeof(preamble_pattern) * 8, &decoded, 160);
            if (decoded.bits_per_row[0] >= 80) {
                ret = tpms_mercedes_decode(decoder, &decoded, 0, 0);
                if (ret > 0)
                    events += ret;
            }
            bitpos += sizeof(preamble_pattern) * 8;
        }
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "pressure_kPa",
        "pressure_PSI",
        "unknown",
        "counter",
        "flags",
        "mic",
        NULL,
};

r_device const tpms_mercedes = {
        .name        = "Mercedes-Benz Sprinter TPMS (FSK PCM)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,
        .long_width  = 52,
        .reset_limit = 53248,
        .decode_fn   = &tpms_mercedes_callback,
        .fields      = output_fields,
};

r_device const tpms_mercedes_mc = {
        .name        = "Mercedes-Benz Sprinter TPMS (FSK MC)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 25,
        .long_width  = 25,
        .reset_limit = 100,
        .decode_fn   = &tpms_mercedes_mc_callback,
        .fields      = output_fields,
};
