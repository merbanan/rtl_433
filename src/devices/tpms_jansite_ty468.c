/** @file
    Jansite TPMS TY-468-eu2 / KKMOON TPMS.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Jansite TPMS TY-468-eu2 / KKMOON TPMS.

https://github.com/merbanan/rtl_433/issues/2025

Same SP372-chip-family encoding as tpms_imars_t240.c (see
https://github.com/merbanan/rtl_433/issues/1820, confirmed the same by
@zuckschwerdt in #2025) -- OOK, Manchester encoded, ~50 us half-bit.

Packet structure: 32-bit raw alternating preamble (0xaaaaaaaa), then
128 raw bits Manchester decoding to 8 bytes (B0..B7).

Decoded byte layout:

    B0 B1 B2 B3 B4 B5 B6 B7

- B7 repeats B0 (tail integrity check)
- (B0 & 0x0f) == (B1 & 0x0f)
- B3 and B4 are each an affine function of B0 (B3 = B0 - k1, B4 = k2 - B0),
  so (B3 + B4) mod 256 == (k2 - k1) mod 256 is a fixed per-unit checksum

Two physical sensor units are confirmed in the issue's captures, each
with its own checksum and calibration constants, both cross-checked
against real ground-truth PSI/C readings posted in the issue (comments
#5 and #32):

    temperature_C = temp_offset - ((B2 + B5) mod 256)
    pressure_kPa  = (pressure_offset - ((B5 + B6) mod 256)) * 2.5

    checksum  temp_offset  pressure_offset
    0xfb      224          273
    0x64      153          201

The 2.5 kPa/unit pressure scale matches tpms_jansite_solar's v2/v3 and
tpms_imars_t240's own (unconfirmed) guess -- likely a hardware-fixed ADC
scale shared across this whole chip family, with checksum/temperature/
pressure offsets all individually calibrated per physical sensor unit.

Only these two units are known; a third unit would need its own
checksum and offsets derived and added here.
*/

static int tpms_jansite_ty468_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[3] = {0xaa, 0xaa, 0xaa};

    if (bitbuffer->num_rows != 1) {
        decoder_logf(decoder, 2, __func__, "Row error");
        return DECODE_ABORT_EARLY;
    }

    int len = bitbuffer->bits_per_row[0];
    int pos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 24);
    if (pos >= len) {
        decoder_logf(decoder, 2, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }

    if (len - pos < 160) {
        decoder_logf(decoder, 2, __func__, "Too short");
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_t packet_bits = {0};
    bitbuffer_manchester_decode(bitbuffer, 0, pos + 32, &packet_bits, 64);
    bitbuffer_invert(&packet_bits);

    if (packet_bits.bits_per_row[0] < 64) {
        return DECODE_FAIL_SANITY;
    }
    uint8_t *b = packet_bits.bb[0];

    if (b[7] != b[0])
        return DECODE_FAIL_SANITY;
    if ((b[0] & 0x0f) != (b[1] & 0x0f))
        return DECODE_FAIL_SANITY;

    int checksum        = (b[3] + b[4]) & 0xff;
    int temp_offset     = 0;
    int pressure_offset = 0;
    if (checksum == 0xfb) {
        temp_offset     = 224;
        pressure_offset = 273;
    }
    else if (checksum == 0x64) {
        temp_offset     = 153;
        pressure_offset = 201;
    }
    else {
        decoder_logf(decoder, 2, __func__, "Checksum error");
        return DECODE_FAIL_MIC;
    }

    float temperature_C = temp_offset - ((b[2] + b[5]) & 0xff);
    float pressure_kPa  = (pressure_offset - ((b[5] + b[6]) & 0xff)) * 2.5f;

    char code_str[7 * 2 + 1];
    snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x%02x%02x",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6]);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Jansite-TY468",
            "type",             "",             DATA_STRING, "TPMS",
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature_C,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure_kPa,
            "code",             "",             DATA_STRING, code_str,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "temperature_C",
        "pressure_kPa",
        "code",
        "mic",
        NULL,
};

r_device const tpms_jansite_ty468 = {
        .name        = "Jansite TPMS TY-468-eu2 / KKMOON TPMS",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 50,
        .long_width  = 50,
        .reset_limit = 200,
        .decode_fn   = &tpms_jansite_ty468_decode,
        .fields      = output_fields,
};
