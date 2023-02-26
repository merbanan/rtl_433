/** @file
    EezTire TPMS.

    Copyright (C) 2023 Bruno OCTAU (ProfBoc75) and Gliebig

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
EezTire TPMS.

Eez RV supported model TPMS10ATC (E618) : https://eezrvproducts.com/shop/ols/products/tpms10atc

S.a issue #2384

Data layout:

    PRE CC IIIIII PP TT FF 00

- PRE : FFFF
- C : 8 bit CheckSum
- I: 24 bit ID
- P: 8 bit pressure  P * 2.5 = Pressure kPa
- T: 8 bit temperature   T - 50 = Temperature C
- F: 8 bit battery (not verified) and deflation pressure (for sure) flags

Raw Data exemple :

    ffff 8b 0d177e 8f 4a 10 00

Format string:

    CHECKSUM:8h ID:24h KPA:8d TEMP:8d FLAG:4b 4h8h

Decode exemple:

   CHECKSUM:8b ID:0d177e KPA:8f TEMP:4a FLAG:10 00

*/

#include "decoder.h"

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
        decoder_log(decoder, 2, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }
    if (pos + 7 * 8 > bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 2, __func__, "Length check fail");
        return DECODE_ABORT_LENGTH;
    }
    uint8_t b[6] = {0};
    uint8_t cc[1] = {0};
    bitbuffer_extract_bytes(bitbuffer, 0, pos+16, cc, sizeof(cc) * 8);
    bitbuffer_extract_bytes(bitbuffer, 0, pos+24, b, sizeof(b) * 8);
    // verify checksum
    if ((add_bytes(b, 6) & 0xff) != cc[0]) {
        decoder_log(decoder, 2, __func__, "Checksum fail");
        return DECODE_FAIL_MIC;
    }
    char id_str[7];
    sprintf(id_str, "%02x%02x%02x", b[0], b[1], b[2]);
    float pressure_kPa  = b[3]*2.5;
    int temperature_C   = b[4]-50;
    int flags = b[5];
    char flags_str[3];
    sprintf(flags_str, "%x", flags);
    /* clang-format off */
    data_t *data = data_make(
                    "model",                       "",    DATA_STRING, "EEZTire-618E",
                    "type",                        "",    DATA_STRING, "TPMS",
                    "id",                          "",    DATA_STRING, id_str,
                    "pressure_kPa",        "Pressure",    DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure_kPa,
                    "temperature_C",    "Temperature",    DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)temperature_C,
                    "flags",                  "Flags",    DATA_STRING, flags_str,
                    "mic",                "Integrity",    DATA_STRING, "CHECKSUM",
                    NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *output_fields[] = {
        "model",
        "type",
        "id",
        "temperature_C",
        "pressure_kPa",
        "flags",
        "mic",
        NULL,
};

r_device const tpms_eezrv = {
        .name        = "EEZTire TPMS10ATC (E618)",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 50,
        .long_width  = 50,
        .reset_limit = 120,
        .decode_fn   = &tpms_eezrv_decode,
        .fields      = output_fields,
};
