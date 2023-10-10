/** @file
    EezTire E618 TPMS.

    Copyright (C) 2023 Bruno OCTAU (ProfBoc75) and Gliebig

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
EezTire E618 TPMS.

Eez RV supported TPMS sensor model E618 : https://eezrvproducts.com/shop/ols/products/tpms-system-e518-anti-theft-replacement-sensor-1-ea

S.a issue #2384, #2657

Data layout:

    PRE CC IIIIII PP TT FF FF

- PRE : FFFF
- C : 8 bit CheckSum
- I: 24 bit ID
- P: 8 bit pressure  P * 2.5 = Pressure kPa
- T: 8 bit temperature   T - 50 = Temperature C
- F: 16  bit battery (not verified), deflation pressure (for sure), pressure MSB

Raw Data example :

    ffff 8b 0d177e 8f 4a 10 00

Format string:

    CHECKSUM:8h ID:24h KPA:8d TEMP:8d FLAG:8b 8b

Decode example:

    CHECKSUM:8b ID:0d177e KPA:8f TEMP:4a FLAG:10 00

*/

static int tpms_eezrv_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // preamble is ffff
    uint8_t const preamble_pattern[] = {0xff, 0xff};

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }
    int pos = 0;
    bitbuffer_invert(bitbuffer);
    pos = bitbuffer_search(bitbuffer, 0, pos, preamble_pattern, sizeof(preamble_pattern) * 8);
    if (pos >= bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 3, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }
    if (pos + 8 * 8 > bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 2, __func__, "Length check fail");
        return DECODE_ABORT_LENGTH;
    }
    uint8_t b[7]  = {0};
    uint8_t cc[1] = {0};
    bitbuffer_extract_bytes(bitbuffer, 0, pos + 16, cc, sizeof(cc) * 8);
    bitbuffer_extract_bytes(bitbuffer, 0, pos + 24, b, sizeof(b) * 8);

    // Verify checksum
    // If the checksum is greater than 0xFF then the MSB is set.
    // It occurs whether the bit is already set or not and was observed when checksum was in the 0x1FF and the 0x2FF range.
    int computed_checksum = add_bytes(b, sizeof(b));
    if (computed_checksum > 0xff) {
        computed_checksum |= 0x80;
    }

    if ((computed_checksum & 0xff) != cc[0]) {
        decoder_log(decoder, 2, __func__, "Checksum fail");
        return DECODE_FAIL_MIC;
    }

    int temperature_C      = b[4] - 50;
    int flags1             = b[5];
    int flags2             = b[6];
    int fast_leak_detected = (flags1 & 0x10);
    int fast_leak_resolved = (flags1 & 0x20);

    int fast_leak = fast_leak_detected && !fast_leak_resolved;
    float pressure_kPa = (((flags2 & 0x01) << 8) + b[3]) * 2.5;

    char id_str[7];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x", b[0], b[1], b[2]);

    char flags_str[5];
    snprintf(flags_str, sizeof(flags_str), "%02x%02x", flags1, flags2);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "EezTire-E618",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure_kPa,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)temperature_C,
            "fast_leak",        "Fast Leak",    DATA_INT,    fast_leak,
            "flags",            "Flags",        DATA_STRING, flags_str,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "temperature_C",
        "pressure_kPa",
        "fast_leak",
        "flags",
        "mic",
        NULL,
};

r_device const tpms_eezrv = {
        .name        = "EezTire E618",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 50,
        .long_width  = 50,
        .reset_limit = 120,
        .decode_fn   = &tpms_eezrv_decode,
        .fields      = output_fields,
};
