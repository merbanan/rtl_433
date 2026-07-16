/** @file
    Jansite TPMS TY588-EU2.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Jansite TPMS TY588-EU2.

A different, Manchester-coded FSK protocol from the older Jansite Solar
TPMS already supported by tpms_jansite_solar.c (different preamble, no
CRC) -- confirmed to be a different wire format, not just a decode bug,
see https://github.com/merbanan/rtl_433/issues/2320.

174 bit FSK PCM packet:

    44 raw (pre-Manchester) preamble bits: 99aa5a6a9aa
    128 Manchester bits -> 8 decoded bytes B0..B7
    2 padding bits

Decoded byte layout:

    B0 B1 B2 B3 B4 B5 B6 B7

- B7 repeats B0 (tail integrity check, used as this decoder's "mic" since
  there is no real CRC).
- B3 = (B0 - 0x1e) mod 256, B4 = (0x4e - B0) mod 256 (structural, always
  true for a genuine message, not device-specific -- an additional sanity
  check).
- (B0 & 0x0f) == (B1 & 0x0f) (structural, ditto).
- temperature_C = (B2 + B5) mod 256 - 139
- pressure_kPa  = ((B5 + B6) mod 256 - 90) * 2.5

Verified against 15 real messages spanning a slow 1.6-2.8 bar pressure
ramp on one real sensor: temperature matches the reported value exactly
on all 15; pressure matches within one 2.5 kPa sensor step on all 15 (the
sensor's own display rounds/truncates more coarsely than its native
resolution).

Sensor ID and battery status are not decoded: B0 and B5 together carry
some per-transmission state (their low nibbles match; their XOR's low
nibble is a constant structural fingerprint, but the high nibble varies
transmission to transmission from the same sensor -- likely a rolling
counter, not a fixed ID). Resolving which bits are a fixed sensor ID
needs samples from a second physical sensor, not available so far. The
raw 7 byte payload is reported as "code" so that distinct sensors can at
least be told apart by their consistently-differing raw bytes.
*/

static int tpms_jansite_ty588_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    uint8_t *b;

    // Preamble is 44 raw bits ("99aa5a6a9aa"); Manchester data follows.
    // Decode 64 output bits: 7 data bytes (B0-B6) + 1 tail byte (B7 = B0).
    bitbuffer_manchester_decode(bitbuffer, row, bitpos + 44, &packet_bits, 64);

    if (packet_bits.bits_per_row[0] < 64) {
        return DECODE_ABORT_LENGTH;
    }
    b = packet_bits.bb[0];

    // Tail byte must equal B0; this is the only integrity check available.
    if (b[7] != b[0]) {
        return DECODE_FAIL_MIC;
    }
    // Structural checks that always hold for this encoding; catch a misaligned/garbled decode.
    if (((b[3] + b[4]) & 0xff) != 0x30 || (b[0] & 0x0f) != (b[1] & 0x0f)) {
        return DECODE_FAIL_SANITY;
    }

    int raw_temp     = (b[2] + b[5]) & 0xff;
    int raw_pressure = (b[5] + b[6]) & 0xff;
    int temperature  = raw_temp - 139;
    int pressure_raw = raw_pressure - 90;

    if (pressure_raw < 0 || temperature < -40 || temperature > 120) {
        return DECODE_FAIL_SANITY;
    }

    char code_str[7 * 2 + 1];
    snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x%02x%02x",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6]);

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",            DATA_STRING, "Jansite-TY588",
            "type",          "",            DATA_STRING, "TPMS",
            "pressure_kPa",  "Pressure",    DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure_raw * 2.5,
            "temperature_C", "Temperature", DATA_FORMAT, "%.0f C",   DATA_DOUBLE, (double)temperature,
            "code",          "",            DATA_STRING, code_str,
            "mic",           "Integrity",   DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_jansite_ty588_decode() */
static int tpms_jansite_ty588_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Preamble: 44 raw bits = "99aa5a6a9aa"; search by the first 24 bits.
    uint8_t const preamble_pattern[3] = {0x99, 0xaa, 0x5a};

    unsigned bitpos = 0;
    int ret         = 0;
    int events      = 0;

    // Need preamble (44 bits) + Manchester data (128 bits) = 172 bits from bitpos.
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 24)) + 172 <=
            bitbuffer->bits_per_row[0]) {

        ret = tpms_jansite_ty588_decode(decoder, bitbuffer, 0, bitpos);
        if (ret > 0)
            events += ret;
        bitpos += 2;
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "pressure_kPa",
        "temperature_C",
        "code",
        "mic",
        NULL,
};

r_device const tpms_jansite_ty588 = {
        .name        = "Jansite TPMS TY588-EU2",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 51,
        .long_width  = 51,
        .reset_limit = 5000,
        .decode_fn   = &tpms_jansite_ty588_callback,
        .fields      = output_fields,
};
