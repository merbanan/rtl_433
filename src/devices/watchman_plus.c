/** @file
    Kingspan/Watchman Plus (Niveau) oil tank monitor, older PWM probe sensor.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Kingspan/Watchman Plus (Niveau) oil tank monitor, older PWM probe sensor.

An older (~2004) probe/pole-based oil tank level sensor. Distinct from the
newer ultrasonic "Watchman Sonic" / "Watchman Sonic Advanced" already
supported by oil_watchman.c / oil_watchman_advanced.c -- this one displays
(and transmits) a single digit 0-9 or "F" (tank full), not a depth in cm.
Manual: https://www.commercialfuelsolutions.co.uk/downloads/manuals/oil_watchman.pdf

Protocol reverse engineered in https://github.com/merbanan/rtl_433/issues/2133
and the earlier https://groups.google.com/g/rtl_433/c/VJSPl7h0848 thread,
cross-checked from scratch against real captures/serial numbers from 3
independent devices.

OOK PWM, raw chip width ~800 us: a "1" bit is 4 chips (~3300 us total), a
"0" bit is 5 chips (~4100 us total) -- short_width/long_width below match
real captures analyzed in both threads (community-tested values, not a
guess).

64 bit message for a normal reading (a longer message is sent for
Full+Bund-alarm or connection-error, not decoded here). Bit offsets are
relative to the end of the preamble match:

    offset  bits  field
    ------  ----  -----------------------------------------------------
     0       8    ID byte 1
     8       2    stuffing marker, always "10"
    10       8    ID byte 2
    18       2    stuffing marker, always "10"
    20       8    ID byte 3
    28       2    stuffing marker, always "10"
    30       4    level (LSB-first: bit weights 1,2,4,8)
    34       3    unknown (observed all-zero on 2 devices, but non-zero
                  on the 3rd -- not a safe sanity check, see below)
    37       1    battery-low
    38       2    stuffing marker, always "10"
    40       4    "complement" nibble, not decoded (see below)
    44       7    tail, not decoded (see below)

- Device ID (byte 1-3 above): reverse the entire 24 raw ID bits (not
  per-byte!) then split into 8 octal digits (0-7 only; the manufacturer
  encodes it BCD-like, so digits 8/9 never occur). This reproduces the
  printed serial number exactly, byte for byte, on all 3 known real
  devices: 007353167, 05073745 and 05404105.
- Level: confirmed against all 10 real digits 0-9 from one device's
  systematic test, plus a sentinel value 10 for "F" (tank full). Values
  11-15 are rejected as invalid (checked below).
- Battery low: confirmed by diffing two otherwise-identical real captures
  (same device, same level) that differ only in battery state.
- The "unknown" 3 bits read all-zero on 2 of the 3 known devices across
  17 of 18 real messages, but the 3rd device's one available message has
  a non-zero value there -- so this field is NOT required to be zero
  (doing so would reject that device's real, valid messages). Likely a
  real per-device or per-condition flag we don't understand yet, not
  padding.
- "Complement" nibble: (K - level) mod 16 for a device-specific constant K
  (confirmed on 2 devices with different K) -- a level-integrity check,
  not a general message checksum. What determines K per device isn't
  known, and one real sample breaks the pattern outright (captured right
  after reseating batteries), so this is NOT used for validation here.
- Final 7 bits: does not behave like a per-message checksum (stays
  constant across messages with different level/ID content on the same
  device); more likely a small status/flags field. Not decoded.

There is no confirmed whole-message checksum for this device. Integrity
here relies on the fixed preamble, the four 2 bit "10" stuffing markers
all matching, and the level being in the valid 0-10 range (structural
validation only, no "mic").

If a preamble match doesn't pass those checks, the decoder tries the next
preamble match later in the same row rather than giving up immediately:
the GitHub issue thread has a first-hand report of a spurious extra bit
(found by hand-decoding a real capture) throwing off the alignment by
one, which a first-match-only search would have no way to recover from.

The bit-level protocol logic above is thoroughly confirmed (18 real
messages from 3 independent devices, decoded via -y test vectors bypassing
pulse detection). The short_width/long_width/reset_limit have NOT been
confirmed end-to-end: real raw .cu8 captures exist (see the GitHub issue
and Google Groups thread) and were tried against this decoder, with the
community's own previously-reported-working flex decoder parameters, but
none produced a clean decode with the current rtl_433 pulse slicer --
rtl_433's automatic pulse-width classifier also does not cleanly separate
the two pulse widths on these particular captures (they end up lumped
into one wide bucket). The short_width/long_width values below are the
best real-world estimate available, not a live-verified pass.
*/

// 13 bit fixed preamble, see file header.
static uint8_t const preamble_pattern[2] = {0xff, 0xf0};

static int watchman_plus_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = 0; // we expect a single row only
    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[row] < 13 + 40) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t const *b = bitbuffer->bb[row];
    unsigned row_len  = bitbuffer->bits_per_row[row];

    int id = 0, level = 0, battery_low = 0;
    int found        = 0;
    unsigned search_start = 0;
    while (search_start + 13 + 40 <= row_len) {
        unsigned match = bitbuffer_search(bitbuffer, row, search_start, preamble_pattern, 13);
        if (match + 13 + 40 > row_len) {
            break;
        }
        unsigned pos = match + 13;
        search_start = match + 1; // try the next (possibly overlapping) preamble match on failure

        int stuff_ok = 1;

        // Raw 24 bit ID, MSB first as transmitted (byte 1, byte 2, byte 3).
        uint32_t id_raw = 0;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 8; j++) {
                id_raw = (id_raw << 1) | bitrow_get_bit(b, pos++);
            }
            if (i < 2) {
                int s0 = bitrow_get_bit(b, pos++);
                int s1 = bitrow_get_bit(b, pos++);
                stuff_ok &= (s0 == 1 && s1 == 0);
            }
        }

        int s0 = bitrow_get_bit(b, pos++);
        int s1 = bitrow_get_bit(b, pos++);
        stuff_ok &= (s0 == 1 && s1 == 0);

        int lvl = 0;
        for (int j = 0; j < 4; j++) {
            lvl |= bitrow_get_bit(b, pos++) << j;
        }

        pos += 3; // unknown bits, not decoded

        int batt_low = bitrow_get_bit(b, pos++);

        int s2 = bitrow_get_bit(b, pos++);
        int s3 = bitrow_get_bit(b, pos++);
        stuff_ok &= (s2 == 1 && s3 == 0);

        if (!stuff_ok || lvl > 10) {
            continue;
        }

        // Reverse the whole 24 bit ID (not per-byte), then split into 8 octal
        // (0-7) digits. reverse32() reverses all 32 bits, so left-align the 24
        // bit ID first and mask the low 24 bits of the result back out.
        uint32_t id_rev = reverse32(id_raw << 8) & 0xffffff;
        int id_val      = 0;
        for (int n = 7; n >= 0; n--) {
            int digit = (id_rev >> (n * 3)) & 0x7;
            id_val    = id_val * 10 + digit;
        }

        id          = id_val;
        level       = lvl;
        battery_low = batt_low;
        found       = 1;
        break;
    }

    if (!found) {
        return DECODE_FAIL_SANITY;
    }

    char level_str[2] = {0};
    if (level <= 9) {
        level_str[0] = (char)('0' + level);
    }
    else {
        level_str[0] = 'F';
    }

    // The 8 digit serial number is printed on the device with leading zeros
    // kept, e.g. "00735316" -- use a string so JSON output keeps them too.
    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%08d", id);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",         DATA_STRING, "Watchman-Plus",
            "id",           "",         DATA_STRING, id_str,
            "level",        "Level",    DATA_STRING, level_str,
            "battery_ok",   "Battery",  DATA_INT,    !battery_low,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "level",
        "battery_ok",
        NULL,
};

r_device const watchman_plus = {
        .name        = "Kingspan/Watchman Plus (Niveau) oil tank monitor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 3299,
        .long_width  = 4107,
        .reset_limit = 5000,
        .decode_fn   = &watchman_plus_decode,
        .fields      = output_fields,
};
