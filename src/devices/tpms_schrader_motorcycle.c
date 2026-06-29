/** @file
    Schrader Motorcycle TPMS sensor.

    Copyright (C) 2026 Bruno OCTAU (ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Schrader Motorcycle TPMS sensor.

issue #3512

Sensor information on the photos:

    PN:3142M / 29074
    R-C-SRD-RDC3
    CNC ID: H-16089
    ANATEL:0651-16-8001
    FCC ID MRXRDC3
    IC: 2546A-RDC3
    M:RDC3                 CIDF15000486

Flex decoder:

    rtl_433 rtl_433 -X 'n=name,m=OOK_MC_ZEROBIT,s=122,l=122,r=375,y=500,bits>=64,preamble={13}7fff'

    codes:   {56}fd420b3ddc5352
    codes:   {56}fd420b3dd95313
    codes:   {56}fd420b3de55316
    codes:   {56}fd420b3de55403

RF signal:

    OOK, Manchester Code with fixed leading zero bit, s=l=122 µs,

Data layout{56} 7 byte:

    Byte Position                            0         1         2         3         4         5         6
    Bit Position                     8765 4321 8765 4321 8765 4321 8765 4321 8765 4321 8765 4321 8765 4321
    Data Layout   [pppp pppp pppp p][FFFF FFII IIII IIII IIII IIII IIII IIPP PPPP PPPP TTTT TTTT]CCCC CCCC
    Sample            7    f    f 8     f    d    4    2    0    b    3    d    d    c    5    3    5    2

- pp:{13} Preamble, fixed 0b0111111111111 = {13}0x7ff8
- FF: {6} Looks fixed, was always 0b111111 from sample codes
- II:{24} Sensor ID, decimal value
- PP:{10} Tire pressure, kPa scale 2
- TT:{8}: Temperature in C offset 50
- CC: CRC-8 bits,poly 0x07, init 0xe0, final xor 0x00 [from previous bytes].

*/

#include "decoder.h"

static int tpms_schrader_motorcycle_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t b[7]  = {0};
    // preamble off 13 bit
    uint8_t const preamble_pattern[] = {0x7f, 0xf8};

    if (bitbuffer->num_rows != 1) {
        decoder_logf(decoder, 2, __func__, "Row error");
        return DECODE_ABORT_EARLY;
    }

    int pos = 0;
    int len = bitbuffer->bits_per_row[0];
    pos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 13); // preamble based on 13 bit
    if (pos >= len) {
        decoder_logf(decoder, 2, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }

    pos += 13;

    if (len - pos < 56) {
        decoder_logf(decoder, 2, __func__, "Too short");
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, pos, b, sizeof(b) * 8);

    decoder_log_bitrow(decoder, 2, __func__, b, 56, "MSG");

    if (crc8(b, 7, 0x07, 0xe0)) {
        decoder_logf(decoder, 2, __func__, "CRC error");
        return DECODE_FAIL_MIC; // crc mismatch
    }

    uint32_t id             = ((uint32_t)(b[0] & 0x03) << 22) | (b[1] << 14) | (b[2] << 6) | (b[3] >> 2);
    uint16_t pressure_raw   = ((b[3] & 0x03) << 8) | b[4];
    float pressure_kPa      = pressure_raw * 0.5f;
    float temperature_C     = (b[5] - 50);

    /* clang-format off */
    data_t *data = data_make(
            "model",               "",                DATA_STRING, "Schrader-Motorcycle",
            "type",                "",                DATA_STRING, "TPMS",
            "id",                  "",                DATA_FORMAT, "%u", DATA_INT, id,
            "pressure_kPa",        "Pressure",        DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure_kPa,
            "temperature_C",       "Temperature",     DATA_FORMAT, "%.1f C",   DATA_DOUBLE, temperature_C,
            "mic",                 "Integrity",       DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "pressure_kPa",
        "temperature_C",
        "mic",
        NULL,
};

r_device const tpms_schrader_motorcycle = {
        .name        = "Schrader Motorcycle TPMS sensor",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 122,
        .long_width  = 122,
        .reset_limit = 375,
        .decode_fn   = &tpms_schrader_motorcycle_decode,
        .fields      = output_fields,
};
