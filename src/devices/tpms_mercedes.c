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

The first known captures are FSK PCM with 52 us pulses. The slicer can leave
15 or 16 leading alignment bits before the 80 bit payload, so this decoder
searches each row for an 80 bit packet with the observed CRC.

Data layout:

    II II II II II TT PP NN FF CC

- I: 40 bit sensor ID
- T: 8 bit temperature, likely deg C offset by 145
- P: 8 bit pressure in PSI
- N: 8 bit rolling counter
- F: 8 bit fixed value, observed as 0x6b
- C: CRC-8, poly 0x2f, init 0xaa

The temperature scale needs confirmation with more captures. The pressure byte
matches the reported cold tire pressure of around 65 PSI.
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

    int temp_raw     = b[5];
    int pressure_psi = b[6];
    int counter      = b[7];
    int fixed        = b[8];

    // Keep this conservative while more tire IDs and temperatures are gathered.
    if (pressure_psi < 10 || pressure_psi > 150 || temp_raw < 80 || temp_raw > 230 || fixed != 0x6b) {
        return DECODE_ABORT_EARLY;
    }

    char id_str[5 * 2 + 1];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4]);

    double temperature_c = (double)temp_raw - 145.0;
    double pressure_kpa  = psi2kpa((float)pressure_psi);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",            DATA_STRING, "Mercedes-Sprinter",
            "type",             "",            DATA_STRING, "TPMS",
            "id",               "",            DATA_STRING, id_str,
            "pressure_PSI",     "Pressure",    DATA_FORMAT, "%d PSI",   DATA_INT,    pressure_psi,
            "pressure_kPa",     "Pressure",    DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, pressure_kpa,
            "temperature_C",    "Temperature", DATA_FORMAT, "%.0f C",   DATA_DOUBLE, temperature_c,
            "temperature_raw",  "",            DATA_INT,    temp_raw,
            "counter",          "",            DATA_INT,    counter,
            "fixed",            "",            DATA_FORMAT, "0x%02x",   DATA_INT,    fixed,
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

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "pressure_PSI",
        "pressure_kPa",
        "temperature_C",
        "temperature_raw",
        "counter",
        "fixed",
        "mic",
        NULL,
};

r_device const tpms_mercedes = {
        .name        = "Mercedes-Benz Sprinter TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,
        .long_width  = 52,
        .reset_limit = 53248,
        .decode_fn   = &tpms_mercedes_callback,
        .fields      = output_fields,
};
