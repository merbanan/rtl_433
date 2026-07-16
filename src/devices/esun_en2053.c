/** @file
    Esun EN2053 two-channel BBQ thermometer.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Esun EN2053 two-channel BBQ thermometer.

Generic-brand two-probe wireless BBQ/meat thermometer, FCC ID 2APN2-EN2053
(Fuzhou Esun Electronic Co.), sold under various names, e.g. as
"Feelle Digital Thermometer" (X0028WROHL).

Reverse engineered from captures and analysis posted in
https://github.com/merbanan/rtl_433/issues/1478.

The sensor sends 40 bits, OOK PPM modulated, repeated 9 times per
transmission with a longer row gap between repeats:

    .short gap  = 1024 us (0 bit)
    .long gap   = 2000 us (1 bit)
    .row gap    = 3952 us
    pulse width = 436 us

Data layout:

    PP 11 12 22 XX

- P: 8 bit fixed preamble/type 0xc0, never observed to change; could
  hold a battery flag on some units (unconfirmed)
- 1: 12 bit probe 1 temperature in Fahrenheit, whole degrees
- 2: 12 bit probe 2 temperature in Fahrenheit, whole degrees
- X: 8 bit checksum, see below

A disconnected probe reads 0xfd6 (4054, or -42 as signed 12 bit).
Temperatures are transmitted in Fahrenheit regardless of the display
unit setting (on the US model tested).

The checksum packs four even-parity flags and a modulo-8 sum of the four
preceding bytes b[0]..b[3]:

- bits 0-2: (b[0] + b[1] + b[2] + b[3]) modulo 8
- bit 3: always 0
- bits 4-7: even parity of b[0], b[1], b[2], b[3] respectively
  (bit set if the byte has an even number of ones)

Verified against 31 distinct known-good messages spanning both channels.
*/

static int esun_en2053_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // The device transmits 9 repeats, require at least 2 matching rows.
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 40);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[row] != 40) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[row];

    if (b[0] != 0xc0) {
        return DECODE_FAIL_SANITY;
    }

    int chk = (b[0] + b[1] + b[2] + b[3]) & 0x07;
    for (int i = 0; i < 4; ++i) {
        chk |= (1 ^ parity8(b[i])) << (4 + i);
    }
    if (chk != b[4]) {
        decoder_log_bitrow(decoder, 1, __func__, b, 40, "checksum fail");
        return DECODE_FAIL_MIC;
    }

    int temp1_raw = (b[1] << 4) | (b[2] >> 4);
    int temp2_raw = ((b[2] & 0x0f) << 8) | b[3];
    int probe1_ok = temp1_raw != 0xfd6; // disconnected probe marker
    int probe2_ok = temp2_raw != 0xfd6;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Esun-EN2053",
            "temperature_1_F",  "Temperature 1", DATA_COND, probe1_ok, DATA_FORMAT, "%d F", DATA_INT, temp1_raw,
            "temperature_2_F",  "Temperature 2", DATA_COND, probe2_ok, DATA_FORMAT, "%d F", DATA_INT, temp2_raw,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "temperature_1_F",
        "temperature_2_F",
        "mic",
        NULL,
};

r_device const esun_en2053 = {
        .name        = "Esun EN2053 two-channel BBQ thermometer",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1024,
        .long_width  = 2000,
        .gap_limit   = 3000,
        .reset_limit = 7500,
        .decode_fn   = &esun_en2053_decode,
        .fields      = output_fields,
};
