/** @file
    Honda (TRW PPA-GF33) TPMS sensor.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Honda (TRW PPA-GF33) TPMS sensor.

FCC-ID: GQ4-36T, part number 42753-SNA-A830-M1. Seen on a 2010 Honda
Insight, likely shared with other Honda models 2009-2016. A different,
unrelated wire format from the Chrysler-fitment tpms_trw.c despite the
same manufacturer -- see issue #3447.

FSK, IEEE 802.3 Manchester coded (`bit1==bit2` ends the run), ~50 us
half-bit. Every real capture starts with the same 23 raw (pre-Manchester)
bits, ending mid-pair on a deliberately invalid `bit1==bit2` transition
that a Manchester decoder can't produce on its own -- a desync marker
usable as a search pattern, not itself part of the Manchester run.

Data layout, 8 bytes after the marker:

    PP TT II II II II FF CC

- P: Pressure, raw * 0.2 PSI. Fits 6 of 7 real pressure points to within
  0.1 PSI; one very-low-pressure point (0.7 PSI actual, reading as if
  ~14.6 PSI) didn't fit, cause not confirmed -- could be a sensor mode
  switch near-empty, or a inaccurate reference reading at that point.
- T: Temperature, offset by 50 C.
- I: 32 bit sensor ID, printed on the sensor and confirmed against a
  commercial TPMS tool reading.
- F: Flags, always 0xe1 parked/stationary in every sample seen so far;
  meaning of individual bits and behavior in motion not yet observed.
- C: CRC-8/SMBUS, poly 0x07, init 0x00, over the preceding 7 bytes.
*/

static int tpms_honda_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    static uint8_t const marker[3] = {0xda, 0xe3, 0x54}; // 23 bits

    unsigned bitpos = bitbuffer_search(bitbuffer, 0, 0, marker, 23);
    if (bitpos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY; // marker not found
    }
    bitpos += 23;

    // 8 bytes Manchester coded is 128 raw bits; reject a truncated capture
    // before decoding rather than relying on the post-decode length check
    // below to catch it.
    if (bitpos + 128 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_t packet_bits = {0};
    bitbuffer_manchester_decode(bitbuffer, 0, bitpos, &packet_bits, 64);
    if (packet_bits.bits_per_row[0] < 64) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = packet_bits.bb[0];

    if (crc8(b, 7, 0x07, 0x00) != b[7]) {
        return DECODE_FAIL_MIC;
    }

    int pressure_raw    = b[0];
    float pressure_psi  = pressure_raw * 0.2f;
    int temperature_raw = b[1];
    int temperature_C   = temperature_raw - 50;
    uint32_t id         = ((uint32_t)b[2] << 24) | (b[3] << 16) | (b[4] << 8) | b[5];
    int flags           = b[6];

    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%08x", id);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Honda-TRW",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "pressure_PSI",     "Pressure",     DATA_FORMAT, "%.1f PSI", DATA_DOUBLE, (double)pressure_psi,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%d C", DATA_INT, temperature_C,
            "flags",            "Flags",        DATA_FORMAT, "%02x", DATA_INT, flags,
            "mic",              "Integrity",    DATA_STRING, "CRC",
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
        "flags",
        "mic",
        NULL,
};

r_device const tpms_honda = {
        .name        = "Honda (TRW PPA-GF33) TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 50,
        .long_width  = 50,
        .reset_limit = 200,
        .decode_fn   = &tpms_honda_callback,
        .fields      = output_fields,
};
