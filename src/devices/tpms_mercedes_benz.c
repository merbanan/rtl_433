/** @file
    Mercedes Benz Sprinter TPMS data.

    Copyright (C) 2026 Bruno OCTAU (ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Mercedes Benz Sprinter TPMS data.

issue #3539:
- 2025 Mercedes Benz Sprinter 4500 TPMS sensor
- Signal is FSK MC and pulse width 25us.
- Preamble {12} 0x002 or 0xff2

Data Layout:

    Preamble    Byte  0  1  2  3  4  5  6  7  8  9
    002 or ff2        ST II II II II PP TT CC FF XX

- ST:  {8} Family/state byte, observed as 0x83 while stationary and 0xa3 while
           moving. These capture-derived values are not known to be exhaustive.
- II: {32} ID / Serial number
- PP:  {8} Pressure, PSI, scale 2.75
- TT:  {8} Temperature, C, offset 51
- CC:  {8} Counter, increase by 1 each message
- FF:  {8} Status, observed as 0x6b, 0xe9, or 0xea
- XX:  {8} CRC-8 of the previous 9 bytes, poly 0x2f, init 0xaa, final XOR 0x00

*/

#include "decoder.h"

static int tpms_mercedes_benz_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x00,0x20}; // 12 bit , 0x002
    uint8_t b[10];

    if (bitbuffer->num_rows != 1) {
        decoder_logf(decoder, 2, __func__, "row error");
        return DECODE_ABORT_EARLY;
    }

    int pos = bitbuffer_search(bitbuffer, 0, 0, preamble, 12);
    if (pos >= bitbuffer->bits_per_row[0]) {
        decoder_logf(decoder, 2, __func__, "Preamble 0x002 not found");
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[0] < 80) {
        decoder_logf(decoder, 1, __func__, "Too short");
        return DECODE_ABORT_LENGTH;
    }

    pos += 12;
    bitbuffer_extract_bytes(bitbuffer, 0, pos, b, 80);

    decoder_log_bitrow(decoder, 2, __func__, b, 80, "MSG");

    if (crc8(b, 10, 0x2f, 0xaa)) {
        decoder_logf(decoder, 1, __func__, "crc error, expected %02x, calculated %02x", b[9], crc8(b, 9, 0x2f, 0xaa));
        return DECODE_FAIL_MIC; // crc mismatch
    }

    if (b[0] != 0x83 && b[0] != 0xa3) {
        decoder_logf(decoder, 1, __func__, "unexpected family/state byte %02x", b[0]);
        return DECODE_FAIL_SANITY;
    }

    char id_str[4 * 2 + 1];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x%02x", b[1], b[2], b[3], b[4]);
    float pressure_PSI = b[5] / 2.75f;
    int temperature_C  = b[6] - 51;
    int counter        = b[7] & 0x1f;
    int flags1         = b[7] >> 5;
    int flags2         = b[8];

    /* clang-format off */
    data_t *data = data_make(
            "model",           "",            DATA_STRING, "MercedesBenz-Sprinter",
            "type",            "",            DATA_STRING, "TPMS",
            "id",              "",            DATA_STRING, id_str,
            "pressure_PSI",    "Pressure",    DATA_FORMAT, "%.1f PSI", DATA_DOUBLE, (double)pressure_PSI,
            "temperature_C",   "Temperature", DATA_FORMAT, "%.1f C",   DATA_DOUBLE, (double)temperature_C,
            "counter",         "Counter",     DATA_INT,    counter,
            "flags1",          "Flags 1",     DATA_FORMAT, "0b%03b",     DATA_INT,    flags1,
            "flags2",          "Flags 2",     DATA_INT,    flags2,
            "mic",             "Integrity",   DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "pressure_PSI",
        "temperature_C",
        "counter",
        "flags1",
        "flags2",
        "mic",
        NULL,
};

r_device const tpms_mercedes_benz = {
        .name        = "Mercedes Benz Sprinter 4500 TPMS sensor",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 25,
        .long_width  = 25,
        .reset_limit = 2000,
        .decode_fn   = &tpms_mercedes_benz_decode,
        .fields      = output_fields,
};
