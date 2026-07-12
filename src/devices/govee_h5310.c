/** @file
    Govee Pool/Spa Thermometer H5310.

    Copyright (C) 2026 Paul Antaki (\@Pablo1)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define GOVEE_H5310_SYNC_LEN      3
#define GOVEE_H5310_MIN_FRAME     7
#define GOVEE_H5310_MAX_FRAME     128
#define GOVEE_H5310_KEY_LEN       128

#define GOVEE_H5310_CRC_POLY      0x1021
#define GOVEE_H5310_CRC_INIT      0x1d0f

#define GOVEE_H5310_MARKER_OFFSET 0
#define GOVEE_H5310_ID_OFFSET     1

// Temperature-update frame: outer length LL = 0x10, decrypts to a 13-byte payload.
#define GOVEE_H5310_TEMP_OUTER_LEN      0x10
#define GOVEE_H5310_TEMP_DECRYPTED_LEN  13
#define GOVEE_H5310_TEMP_MARKER         0x11
#define GOVEE_H5310_TEMP_BATTERY_OFFSET 6
#define GOVEE_H5310_TEMP_LO_OFFSET      7
#define GOVEE_H5310_TEMP_HI_OFFSET      8

// Periodic-update frame: outer length LL = 0x3d, decrypts to a 58-byte payload.
// Leading layout mirrors the temperature-update frame shifted left by one byte
// (no constant 0x04 byte at dec[5]); the remainder is a rolling counter/nonce
// followed by zero padding.
#define GOVEE_H5310_PERIODIC_OUTER_LEN      0x3d
#define GOVEE_H5310_PERIODIC_DECRYPTED_LEN  58
#define GOVEE_H5310_PERIODIC_MARKER         0x1b
#define GOVEE_H5310_PERIODIC_BATTERY_OFFSET 5
#define GOVEE_H5310_PERIODIC_LO_OFFSET      6
#define GOVEE_H5310_PERIODIC_HI_OFFSET      7

// Status-reply frame: outer length LL = 0x1f, decrypts to a 28-byte payload.
// Leading layout matches the temperature-update frame (device ID, shared
// material, battery, temperature), but lacks the constant 0x04 byte at
// dec[5] and carries additional trailing bytes. Sent in reply to a short
// LL=0x0c poll frame (not decoded by this driver).
#define GOVEE_H5310_STATUS_OUTER_LEN      0x1f
#define GOVEE_H5310_STATUS_DECRYPTED_LEN  28
#define GOVEE_H5310_STATUS_MARKER         0x71
#define GOVEE_H5310_STATUS_BATTERY_OFFSET 5
#define GOVEE_H5310_STATUS_LO_OFFSET      6
#define GOVEE_H5310_STATUS_HI_OFFSET      7

// Ping/connectivity frame: outer length LL = 0x1c, decrypts to a 25-byte payload.
// Carries no battery or temperature data; observed as a connectivity check-in
// (e.g. "Network Test", unit-display change, "long battery life" toggle).
// Unlike the temperature-bearing frames, the device ID and shared material
// are swapped: shared material is at dec[1-2], device ID at dec[3-4].
#define GOVEE_H5310_PING_OUTER_LEN     0x1c
#define GOVEE_H5310_PING_DECRYPTED_LEN 25
#define GOVEE_H5310_PING_MARKER        0x70
#define GOVEE_H5310_PING_ID_OFFSET     3

// T_C = (raw - OFFSET) / SLOPE; raw is little-endian u16. Sign below 0 C unverified.
#define GOVEE_H5310_TEMP_SLOPE  10.0
#define GOVEE_H5310_TEMP_OFFSET 33168

// Plausible range for a pool/spa/ambient thermometer. Other Govee devices in
// this family share the same outer frame envelope (sync words, XOR key, CRC)
// and can, with a different payload layout, pass this decoder's length and
// marker checks yet yield a nonsensical temperature. Rejecting values outside
// this range discards such frames without needing to identify the foreign
// device by ID.
#define GOVEE_H5310_TEMP_MIN_C -20.0f
#define GOVEE_H5310_TEMP_MAX_C 60.0f

static uint8_t const govee_h5310_sync[]       = {0x2c, 0x4c, 0x4a};
static uint8_t const govee_h5310_sync_skew1[] = {0x16, 0x26, 0x25};
static uint8_t const govee_h5310_key[GOVEE_H5310_KEY_LEN + 1] =
        "s6amyEvO8UslCY0eZjgc2S6APCVLgLxzFvL2Z5GWPW7fKVjy2oAU6uiKU3lZCHm62VYQQuCtgxzPgGd8UDRPVZpDRAsh5EdYq1E4j4morJ3vd6tWx8BiWOLDc2I8wKUK";

/**
Govee Pool/Spa Thermometer H5310.

Based on the Govee H5059 decoder by Reece Neff <reeceneff@gmail.com>
(rtl_433 PR #3493). That work reverse-engineered the shared Govee FSK
protocol used by this device family -- the 2c 4c 4a sync words, the
128-byte XOR key, and the CRC-16/AUG-CCITT framing -- from a JTAG
firmware dump of an H5059. This H5310 decoder reuses that protocol
foundation and adds the LL=0x10 temperature-update, LL=0x3d
periodic-update, and LL=0x1c ping/connectivity payload layouts and the
temperature conversion.

Modulation, timing, and transmission:

The device uses FSK pulse PCM encoding, the same sync words, XOR key, and
CRC-16/AUG-CCITT framing as the Govee H5059 water leak detector. Those
shared protocol details were reverse-engineered by Reece Neff for the
H5059 (rtl_433 PR #3493) and are reused here.
- Demod mode in rtl_433 is FSK_PULSE_PCM.
- short_width = 100 us and long_width = 100 us (PCM symbol timing).
- reset_limit = 2000 us.
- The sensor transmits autonomously roughly every 10 minutes, plus on
  significant temperature change.

Sensor uplink frame format:

    2C 4C 4A LL SS [ciphertext...] CC CC

- 2C 4C 4A: sync word
- LL: outer frame length in bytes (SS + ciphertext + CRC)
- SS: XOR stream start offset (seed)
- ciphertext: encrypted payload bytes
- CC CC: CRC-16/AUG-CCITT, poly=0x1021, init=0x1d0f

This decoder handles four frame types. Three of them -- temperature-update,
periodic-update, and status-reply -- share the same leading payload layout
(device ID, shared ID material, battery, temperature), shifted by one byte
between temperature-update and the other two; the ping frame shares only the
device ID / shared material fields:

Temperature-update frame, LL == 0x10 (16), 13-byte decrypted payload. This
frame appears to be triggered by a unit-change/forced-report action rather
than sent on the autonomous schedule, and is comparatively rare:

    byte:  0   1  2   3  4   5    6     7      8     9   10  11 12
    val :  11  <id-2>  9c b2  04   bat  TL     TH    cc  ff  00 00

- dec[0]: frame marker, constant 0x11
- dec[1-2]: device ID (e.g. 71 b0 = Spa, f8 a7 = Pool)
- dec[3-4]: constant 9c b2 (shared device-ID material)
- dec[5]: constant 0x04
- dec[6]: battery percent (0x64 = 100)
- dec[7-8]: temperature raw, little-endian u16
- dec[9-12]: cc ff 00 00 (constants / padding)

Periodic-update frame, LL == 0x3d (61), 58-byte decrypted payload. This is the
sensor's autonomous broadcast, observed at exactly 10-minute intervals. The
leading bytes mirror the temperature-update layout shifted left by one byte
(no constant 0x04 byte), followed by a rolling counter/nonce and zero padding:

    byte:  0   1  2   3  4   5    6     7      8     9   10  11  12..57
    val :  1b  <id-2>  9c b2  bat  TL    TH    cc  ff  00  00  <rolling/nonce, then zero padding>

- dec[0]: frame marker, constant 0x1b
- dec[1-2]: device ID (e.g. 71 b0 = Spa, f8 a7 = Pool)
- dec[3-4]: constant 9c b2 (shared device-ID material)
- dec[5]: battery percent
- dec[6-7]: temperature raw, little-endian u16
- dec[8-11]: cc ff 00 00 (constants / padding)
- dec[12+]: rolling counter/nonce, then zero padding

Status-reply frame, LL == 0x1f (31), 28-byte decrypted payload. Sent in
response to a short LL=0x0c poll/status-request frame from the app or
gateway (that poll frame is not decoded by this driver). The leading bytes
match the temperature-update layout shifted left by one byte (no constant
0x04 byte), like the periodic-update frame, followed by a constant and a
few bytes that vary between replies, then zero padding:

    byte:  0   1  2   3  4   5    6     7      8     9   10  11  12  13..27
    val :  71  <id-2>  9c b2  bat  TL    TH    cc  ff  <varies, dec[10-12]>  00-padded

- dec[0]: frame marker, constant 0x71
- dec[1-2]: device ID (e.g. 71 b0 = Spa, f8 a7 = Pool)
- dec[3-4]: constant 9c b2 (shared device-ID material)
- dec[5]: battery percent
- dec[6-7]: temperature raw, little-endian u16 -- matches the concurrent
  temperature-update/periodic-update value
- dec[8-9]: cc ff (constants)
- dec[10-12]: varies between replies -- not yet decoded (counter/nonce
  candidate)
- dec[13-27]: zero padding

Before this frame type was added, LL=0x1f frames were decrypted successfully
by the H5059 decoder (same shared framing/key/CRC) but reported as model
"Govee-H5059" with event "Unknown" -- claiming them here is both more accurate
(this frame type has only been observed from H5310-family devices) and
resolves that cross-model mislabeling.

Ping/connectivity frame, LL == 0x1c (28), 25-byte decrypted payload. Carries
no battery or temperature data. Observed correlating with app-side actions
(Network Test, unit-display change, "long battery life" toggle on/off) for
the Pool sensor, and also sent by the Spa; never observed from the H5059 leak
detector, which only sends msg_class 0x11 telemetry:

    byte:  0   1  2   3  4   5    6    7   8   9   10  11..24
    val :  70  9c b2  <id-2>  6a   2e  <varies, counter/nonce + flag>  00-padded

- dec[0]: frame marker, constant 0x70
- dec[1-2]: constant 9c b2 (shared device-ID material) -- note this is in the
  opposite order from the temperature-bearing frames, where the shared
  material follows the device ID
- dec[3-4]: device ID (e.g. 71 b0 = Spa, f8 a7 = Pool)
- dec[5]: constant 0x6a
- dec[6]: constant 0x2e
- dec[7-10]: varies between frames -- likely a counter/nonce plus a status
  flag, not yet decoded
- dec[11-24]: zero padding

Before this frame type was added, LL=0x1c frames were decrypted successfully
by the H5059 decoder (same shared framing/key/CRC) but reported as model
"Govee-H5059" with event "Unknown" -- claiming them here is both more accurate
(this frame type has only been observed from H5310-family devices) and
resolves that cross-model mislabeling.

Device ID: the emitted `id_wire` is the full 32-bit on-wire ID (e.g.
f8 a7 9c b2 -> 0xf8a79cb2) and `id` is the same value with its two 16-bit
words swapped (0x9cb2f8a7), matching the rest of the Govee family
(govee_h5059). The low 16 bits (9c b2) are shared family material; the
device-distinguishing half is f8a7 (Pool) / 71b0 (Spa).

Temperature formula (applies to the three temperature-bearing frame types
above, using each frame's
own low/high temperature byte offsets):

    raw  = lo_byte + (hi_byte << 8)
    T_C  = (raw - 33168) / 10.0

The slope (1 raw count = 0.1 C) comes from a 43->27 C sweep (R^2 = 0.986). The
offset was originally fit as 33178 from that sweep, but live captures on
2026-06-13 found decoded temperatures consistently read 1.0 C low versus the
sensor's own display (4 data points, two different temperatures, across both
frame types above) -- so the offset was corrected to 33168. Sign behavior
below 0 C is unverified; the formula predicts raw keeps counting down, but no
cold-point capture has confirmed this yet.

The wire value is always Celsius-based regardless of the sensor's selected
display unit. Toggling the on-device display between C and F (25.8 C <->
78.5 F) produced identical decoded values (25.800 C) before and after the
toggle, in both directions -- the C/F setting only affects the sensor's local
display, not the transmitted payload, so no unit-aware handling is needed
here.

`battery_pct` tracks the sensor's bar-graph battery indicator: 100% (0x64) at
full bars, 65% at 3 bars, 47% at 2 bars, and 20% at 1 bar, all observed live
by running a set of batteries down. At 0 bars (critical), the sensor stopped
transmitting RF entirely -- no frame with battery_pct near 0 was ever
received. So `battery_ok = battery_pct > 0` is effectively always true for
any frame this decoder actually sees; the real "low battery" signal is the
sensor going silent, which is a job for rtl_433's downstream
"not seen recently" handling, not this decoder. The placeholder is left as-is
since there's no in-band low-battery state to detect.

Other frames in this family (e.g. LL == 0x0c short poll/status, and the H5059
leak-detector's own msg_class 0x01/0x02/0x11 telemetry) are not recognized by
this decoder and are ignored; the H5059 decoder may report some of these as
event "Unknown". The H5310 decoder runs at the default priority (before
H5059's reduced priority, see govee_h5059.c) so it gets first access to
frames in this family.
*/
static int govee_h5310_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t frame[GOVEE_H5310_MAX_FRAME];
    uint8_t dec[GOVEE_H5310_MAX_FRAME];

    int row           = -1;
    unsigned sync_pos = 0;

    // Stage 1: find a row that contains either the native sync or the known skewed sync.
    for (int r = 0; r < bitbuffer->num_rows; ++r) {
        if (bitbuffer->bits_per_row[r] < 8 * GOVEE_H5310_MIN_FRAME) {
            continue;
        }

        unsigned pos = bitbuffer_search(bitbuffer, r, 0, govee_h5310_sync, GOVEE_H5310_SYNC_LEN * 8);
        if (pos < bitbuffer->bits_per_row[r]) {
            row      = r;
            sync_pos = pos;
            break;
        }

        // The device preamble is MSB-first; depending on demod lock, bytes can
        // arrive shifted by one bit, turning 2c 4c 4a into 16 26 25.
        unsigned skew_pos = bitbuffer_search(bitbuffer, r, 0, govee_h5310_sync_skew1, GOVEE_H5310_SYNC_LEN * 8);
        if (skew_pos < bitbuffer->bits_per_row[r]) {
            row      = r;
            sync_pos = skew_pos + 1;
            break;
        }
    }

    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    // Stage 2: validate envelope and reject malformed messages before payload parsing.
    sync_pos += GOVEE_H5310_SYNC_LEN * 8;

    unsigned bits_after_sync = bitbuffer->bits_per_row[row] - sync_pos;
    if (bits_after_sync < 8 * 4) {
        return DECODE_ABORT_LENGTH;
    }

    unsigned bytes_after_sync = bits_after_sync / 8;
    if (bytes_after_sync > GOVEE_H5310_MAX_FRAME) {
        bytes_after_sync = GOVEE_H5310_MAX_FRAME;
    }

    bitbuffer_extract_bytes(bitbuffer, row, sync_pos, frame, bytes_after_sync * 8);

    uint8_t outer_len = frame[0];

    int is_temp_frame     = (outer_len == GOVEE_H5310_TEMP_OUTER_LEN);
    int is_periodic_frame = (outer_len == GOVEE_H5310_PERIODIC_OUTER_LEN);
    int is_status_frame   = (outer_len == GOVEE_H5310_STATUS_OUTER_LEN);
    int is_ping_frame     = (outer_len == GOVEE_H5310_PING_OUTER_LEN);
    // Only the temperature-update, periodic-update, status-reply, and ping
    // frames have a fixed outer length we recognize; ignore all other frame
    // types in the family (short poll, leak telemetry) without erroring.
    if (!is_temp_frame && !is_periodic_frame && !is_status_frame && !is_ping_frame) {
        return DECODE_ABORT_EARLY;
    }

    if (bytes_after_sync < (unsigned)(1 + outer_len)) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t seed      = frame[1];
    unsigned enc_len  = outer_len - 3;
    unsigned crc_offs = 2 + enc_len;

    uint16_t crc_calc = crc16(&frame[2], enc_len, GOVEE_H5310_CRC_POLY, GOVEE_H5310_CRC_INIT);
    uint16_t crc_recv = ((uint16_t)frame[crc_offs] << 8) | frame[crc_offs + 1];
    if (crc_calc != crc_recv) {
        return DECODE_FAIL_MIC;
    }

    for (unsigned i = 0; i < enc_len; ++i) {
        dec[i] = frame[2 + i] ^ govee_h5310_key[(i + seed) % GOVEE_H5310_KEY_LEN];
    }

    // A CRC-valid frame should carry the marker matching its outer length;
    // guard against any other message subtype that happens to share this length.
    uint8_t expected_marker = is_temp_frame       ? GOVEE_H5310_TEMP_MARKER
                              : is_periodic_frame ? GOVEE_H5310_PERIODIC_MARKER
                              : is_status_frame   ? GOVEE_H5310_STATUS_MARKER
                                                  : GOVEE_H5310_PING_MARKER;
    if (dec[GOVEE_H5310_MARKER_OFFSET] != expected_marker) {
        return DECODE_ABORT_EARLY;
    }

    // Ping frame has shared material and device ID in the opposite order from the
    // temperature-bearing frames; reassemble to the canonical id_wire layout.
    uint32_t id_wire;
    if (is_ping_frame) {
        id_wire = ((uint32_t)dec[GOVEE_H5310_PING_ID_OFFSET] << 24) | ((uint32_t)dec[GOVEE_H5310_PING_ID_OFFSET + 1] << 16) | ((uint32_t)dec[GOVEE_H5310_ID_OFFSET] << 8) | dec[GOVEE_H5310_ID_OFFSET + 1];
    }
    else {
        id_wire = ((uint32_t)dec[GOVEE_H5310_ID_OFFSET] << 24) | ((uint32_t)dec[GOVEE_H5310_ID_OFFSET + 1] << 16) | ((uint32_t)dec[GOVEE_H5310_ID_OFFSET + 2] << 8) | dec[GOVEE_H5310_ID_OFFSET + 3];
    }
    uint32_t id = ((id_wire & 0xffff) << 16) | ((id_wire >> 16) & 0xffff);

    int battery_pct = 0;
    uint16_t raw    = 0;
    char const *event;
    if (is_temp_frame) {
        battery_pct = dec[GOVEE_H5310_TEMP_BATTERY_OFFSET];
        raw         = dec[GOVEE_H5310_TEMP_LO_OFFSET] | ((uint16_t)dec[GOVEE_H5310_TEMP_HI_OFFSET] << 8);
        event       = "Temperature Update";
    }
    else if (is_periodic_frame) {
        battery_pct = dec[GOVEE_H5310_PERIODIC_BATTERY_OFFSET];
        raw         = dec[GOVEE_H5310_PERIODIC_LO_OFFSET] | ((uint16_t)dec[GOVEE_H5310_PERIODIC_HI_OFFSET] << 8);
        event       = "Periodic Update";
    }
    else if (is_status_frame) {
        battery_pct = dec[GOVEE_H5310_STATUS_BATTERY_OFFSET];
        raw         = dec[GOVEE_H5310_STATUS_LO_OFFSET] | ((uint16_t)dec[GOVEE_H5310_STATUS_HI_OFFSET] << 8);
        event       = "Status";
    }
    else {
        event = "Ping";
    }

    float temperature_c = ((float)raw - GOVEE_H5310_TEMP_OFFSET) / GOVEE_H5310_TEMP_SLOPE;

    if (!is_ping_frame && (temperature_c < GOVEE_H5310_TEMP_MIN_C || temperature_c > GOVEE_H5310_TEMP_MAX_C)) {
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Govee-H5310",
            "id",               "",             DATA_FORMAT, "%08x", DATA_INT, id,
            "id_wire",          "",             DATA_FORMAT, "%08x", DATA_INT, id_wire,
            "event",            "",             DATA_STRING, event,
            "battery_ok",       "Battery",      DATA_COND, !is_ping_frame, DATA_INT,    battery_pct > 0,
            "battery_pct",      "Battery",      DATA_COND, !is_ping_frame, DATA_INT,    battery_pct,
            "temperature_C",    "Temperature",  DATA_COND, !is_ping_frame, DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)temperature_c,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/* Output fields for -F csv column order and optional field filtering. */
static char const *const output_fields[] = {
        "model",
        "id",
        "id_wire",
        "event",
        "battery_ok",
        "battery_pct",
        "temperature_C",
        "mic",
        NULL,
};

/* Device registration and demod timing profile. */
r_device const govee_h5310 = {
        .name        = "Govee Pool/Spa Thermometer H5310",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 2000,
        .decode_fn   = &govee_h5310_decode,
        .fields      = output_fields,
};
