/** @file
    Auriol HG04641A temperature station.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Auriol HG04641A temperature station (Lidl, IAN 307350).

https://github.com/merbanan/rtl_433/issues/2014

OOK, PPM (distance coding): fixed ~510 us pulse, followed by a ~980 us
gap for a 0-bit or a ~1976 us gap for a 1-bit. Each transmission repeats
the same 36-bit message 4 times, with the last repeat sometimes
truncated by one bit.

Data layout (nibbles):

    II II F TTT C

- I: 16 bit id, fixed per physical unit (unchanged across a battery pull
  in the one capture set that tested this -- not confirmed to be a
  rolling code)
- F: 4 bit flags -- bit 3 (0x8) is battery_low, confirmed by an
  old-vs-fresh-battery comparison in the issue; bit 0 is always set and
  bits 1-2 always clear in every capture seen so far (channel bits, never
  actually tested by switching channel? unconfirmed either way)
- T: 12 bit temperature, 2's complement, scale 0.1 C -- confirmed against
  a freezer test (-14.1 C) and two above-zero readings
- C: 4 bit checksum -- sum of the preceding 8 nibbles, mod 16

The checksum is only 4 bits, far too weak on its own to reject noise
reliably (confirmed empirically: enabling this decoder against the full
rtl_433_tests corpus produces two dozen false positives on unrelated
weather-sensor protocols that happen to share similar-enough OOK/PPM
timing). The flags-nibble and temperature-range checks above are cheap,
well-supported-by-available-data mitigations, not a fix for the
underlying weak checksum -- a real message from a channel setting this
decoder hasn't seen could in principle still get rejected.
*/

static int auriol_hg04641a_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 36);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[row] < 36) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[5];
    bitbuffer_extract_bytes(bitbuffer, row, 0, b, 36);
    b[4] >>= 4; // only the top nibble of the 5th byte is real (36 bits = 9 nibbles)

    int sum = (b[0] >> 4) + (b[0] & 0xf) + (b[1] >> 4) + (b[1] & 0xf)
            + (b[2] >> 4) + (b[2] & 0xf) + (b[3] >> 4) + (b[3] & 0xf);
    if ((sum & 0xf) != b[4]) {
        return DECODE_FAIL_MIC;
    }

    int flags = b[2] >> 4;
    // bits 1-2 are 0 in every real capture seen so far (only bit 0, always
    // set, and bit 3, the battery flag, ever vary) -- a weak but free extra
    // check on top of the protocol's own narrow 4-bit checksum.
    if ((flags & 0x6) != 0 || !(flags & 0x1)) {
        return DECODE_FAIL_SANITY;
    }
    int battery_ok = !(flags & 0x8);

    int id       = (b[0] << 8) | b[1];
    int temp_raw = (int16_t)(((b[2] & 0x0f) << 12) | (b[3] << 4)); // sign extend via top-aligned int16_t
    int temp_decic = temp_raw >> 4; // signed, in units of 0.1 C

    // no plausible reading is anywhere near this range; a real message
    // should never trip it, but it catches most random noise that
    // otherwise passes the narrow 4-bit checksum by chance.
    if (temp_decic < -400 || temp_decic > 600) {
        return DECODE_FAIL_SANITY;
    }
    float temp_c = temp_decic * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Auriol-HG04641A",
            "id",               "",             DATA_FORMAT, "%04x", DATA_INT, id,
            "battery_ok",       "Battery",      DATA_INT,    battery_ok,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)temp_c,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "mic",
        NULL,
};

r_device const auriol_hg04641a = {
        .name        = "Auriol HG04641A temperature station",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 980,
        .long_width  = 1976,
        .gap_limit   = 2500,
        .reset_limit = 5000,
        .decode_fn   = &auriol_hg04641a_decode,
        .fields      = output_fields,
};
