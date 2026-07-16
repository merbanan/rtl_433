/** @file
    Oregon Scientific WMR500 professional All-In-One weather station.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Oregon Scientific WMR500 professional All-In-One weather station.

https://github.com/merbanan/rtl_433/issues/2407

FSK, PCM, ~26 us bit width, levels inverted relative to the other Oregon
Scientific protocols already in this codebase. A 40 bit preamble/sync
(0x552c6e2c6e in the inverted domain) precedes each message.

Two message kinds are seen on air, sharing the same 14 byte header:

    offset  bytes  field
    ------  -----  ------------------------------------------------------
     0       1     LEN: 14 (short message) or 25 (long message)
     1       1     fixed, 0xfe
     2       1     TYPE: varies message to message, not decoded
     3-7     5     fixed, 25 1d f9 dd df
     8-9     2     device ID (only one physical unit's value is known; one
                   message in the one real session available differs from
                   the rest by a single low bit, cause not confirmed --
                   could be a battery/status flag sharing these bytes, or
                   a rare CRC-16 false accept of a corrupted message)
     10      1     not decoded (part of an undecoded payload, see below)
     11      1     fixed, 0xc6
     12      1     not decoded
     13      1     fixed, 0x4f

After the header the two kinds diverge:

- LEN=14 (17 bytes total): byte 14 is a parity byte (poly 0x01) over
  bytes 0-13; bytes 15-16 are a CRC-16 (poly 0x8005, init 0x4ed0) over
  bytes 0-14. The rest of the payload (bytes 10 and 12 above) is not
  decoded -- likely wind speed/direction given how fast it changes
  message to message, but this is not confirmed, so this message kind is
  not reported by this decoder at all.
- LEN=25 (28 bytes total): byte 16 is humidity, byte 14 is temperature
  (see below); bytes 17-25 are constant in all data seen so far (not
  decoded); bytes 26-27 are a CRC-16 (poly 0x8005, init 0x1a4c) over
  bytes 0-25.

Both CRCs are confirmed against 18 real messages (10 short, 8 long) from
the issue.

Humidity is confirmed exactly against the two real reference readings
posted in the issue (start and end of a slow measurement session):

    humidity_percent = 208 - b[16]

This matches 59% and 49% exactly (not just approximately) at both ends of
that real session.

Temperature is NOT independently confirmed the way humidity is -- only
those same 2 reference points are available (7.1 C and 9.9 C at the same
two ends of that session), which is the bare minimum to fit a 2 parameter
(offset, scale) linear model, with no slack left to check it against a
third point:

    temperature_C = (b[14] - 169) * 0.7

This fits both ends within 0.1-0.2 C (plausible display rounding), but
0.7 C/count is not a clean constant, so the true relationship (which
could be non-linear, e.g. an uncorrected thermistor curve) may differ in
detail even though this is directionally and roughly quantitatively
right. Treat this field as a rough estimate pending a capture spanning a
wider, precisely known temperature range.
*/

static int oregon_scientific_wmr500_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // preamble/sync, in the inverted domain (see bitbuffer_invert() below).
    uint8_t const preamble_pattern[5] = {0x55, 0x2c, 0x6e, 0x2c, 0x6e};

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    bitbuffer_invert(bitbuffer);

    unsigned row_len = bitbuffer->bits_per_row[0];
    unsigned pos      = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 40);
    if (pos >= row_len) {
        return DECODE_ABORT_EARLY;
    }
    pos += 40;
    if (pos + 8 > row_len) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[28]        = {0};
    unsigned avail_bytes = (row_len - pos) / 8;
    if (avail_bytes > sizeof(b)) {
        avail_bytes = sizeof(b);
    }
    bitbuffer_extract_bytes(bitbuffer, 0, pos, b, avail_bytes * 8);

    int len = b[0];
    int total_bytes;
    uint16_t crc_init;
    if (len == 14) {
        total_bytes = 17;
        crc_init    = 0x4ed0;
    }
    else if (len == 25) {
        total_bytes = 28;
        crc_init    = 0x1a4c;
    }
    else {
        return DECODE_ABORT_EARLY;
    }
    if (avail_bytes < (unsigned)total_bytes) {
        return DECODE_ABORT_LENGTH;
    }

    uint16_t crc_calc = crc16(b, total_bytes - 2, 0x8005, crc_init);
    uint16_t crc_msg  = (b[total_bytes - 2] << 8) | b[total_bytes - 1];
    if (crc_calc != crc_msg) {
        return DECODE_FAIL_MIC;
    }

    if (len == 14) {
        // Short message: no temperature/humidity payload decoded (see file header).
        return DECODE_ABORT_EARLY;
    }

    int device_id = (b[8] << 8) | b[9];

    int humidity = 208 - b[16];
    if (humidity < 0 || humidity > 100) {
        return DECODE_FAIL_SANITY;
    }
    float temperature_C = ((float)b[14] - 169.0f) * 0.7f;

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",             DATA_STRING, "Oregon-WMR500",
            "id",            "",             DATA_FORMAT, "%04x", DATA_INT, device_id,
            "temperature_C", "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)temperature_C,
            "humidity",      "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",           "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device const oregon_scientific_wmr500 = {
        .name        = "Oregon Scientific WMR500 weather station",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 26,
        .long_width  = 26,
        .reset_limit = 312,
        .decode_fn   = &oregon_scientific_wmr500_decode,
        .fields      = output_fields,
};
