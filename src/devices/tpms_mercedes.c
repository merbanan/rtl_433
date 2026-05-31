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

    II II II II II PP TT FN SS CC

- I: 40 bit sensor ID
- P: 8 bit pressure, PSI = raw / 2.75
- T: 8 bit temperature, degrees C = raw - 51 in the current captures
- F: 3 bit flags/status, observed as 0x02 in the current captures
- N: 5 bit rolling counter, observed 1..31 and never 0
- S: 8 bit fixed/status value, observed as 0x6b in the current captures
- C: CRC-8, poly 0x2f, init 0xaa

More captures across temperatures and sensor states are needed to confirm the
temperature offset and all possible flag/status values. The pressure scaling
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

    int pressure_raw    = b[5];
    int temperature_raw = b[6];
    int flags           = b[7] >> 5;
    int counter         = b[7] & 0x1f;
    int status          = b[8];
    double pressure_psi = pressure_raw / 2.75;
    double pressure_kpa = psi2kpa((float)pressure_psi);
    double temperature_c = temperature_raw - 51.0;

    // Keep the provisional status filter while more sample sets are gathered.
    if (pressure_psi < 10.0 || pressure_psi > 150.0 || counter == 0 || status != 0x6b) {
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
            "temperature_C",    "Temperature", DATA_FORMAT, "%.0f C",    DATA_DOUBLE, temperature_c,
            "flags",            "",            DATA_FORMAT, "0x%x",     DATA_INT,    flags,
            "counter",          "",            DATA_INT,    counter,
            "status",           "",            DATA_FORMAT, "0x%02x",   DATA_INT,    status,
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
        "temperature_C",
        "flags",
        "counter",
        "status",
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
