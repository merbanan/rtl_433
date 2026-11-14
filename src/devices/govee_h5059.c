/** @file
    Govee Water Leak Detector H5059.

    Copyright (C) 2026 Reece Neff <reeceneff@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define GOVEE_H5059_SYNC_LEN            3
#define GOVEE_H5059_MIN_FRAME           7
#define GOVEE_H5059_MAX_FRAME           128
#define GOVEE_H5059_KEY_LEN             128

#define GOVEE_H5059_MIN_DECRYPTED_LEN   19

#define GOVEE_H5059_CRC_POLY            0x1021
#define GOVEE_H5059_CRC_INIT            0x1d0f

#define GOVEE_H5059_MSG_CLASS_OFFSET    0
#define GOVEE_H5059_ID_OFFSET           1
#define GOVEE_H5059_PRODUCT_OFFSET      9
#define GOVEE_H5059_BATTERY_OFFSET      11
#define GOVEE_H5059_SUBTYPE_OFFSET      13
#define GOVEE_H5059_LEAK_TOP_OFFSET     14
#define GOVEE_H5059_LEAK_BOTTOM_OFFSET  15
#define GOVEE_H5059_LEAK_ALARM_OFFSET   17

#define GOVEE_H5059_LEAK_FLAG_ACTIVE    0x01

#define GOVEE_H5059_MSG_CLASS_TELEMETRY 0x11
#define GOVEE_H5059_MSG_CLASS_PAIRING   0x01
#define GOVEE_H5059_MSG_CLASS_OTHER     0x02

#define GOVEE_H5059_SUBTYPE_POWER_ON    0x00
#define GOVEE_H5059_SUBTYPE_BUTTON      0x05
#define GOVEE_H5059_SUBTYPE_LEAK        0x06
#define GOVEE_H5059_SUBTYPE_POST_ALARM  0x07

#define GOVEE_H5059_PRODUCT_H5059       0x1e
#define GOVEE_H5059_PRODUCT_H5830       0x13

static uint8_t const govee_h5059_sync[]       = {0x2c, 0x4c, 0x4a};
static uint8_t const govee_h5059_sync_skew1[] = {0x16, 0x26, 0x25};
static uint8_t const govee_h5059_key[GOVEE_H5059_KEY_LEN + 1] =
        "s6amyEvO8UslCY0eZjgc2S6APCVLgLxzFvL2Z5GWPW7fKVjy2oAU6uiKU3lZCHm62VYQQuCtgxzPgGd8UDRPVZpDRAsh5EdYq1E4j4morJ3vd6tWx8BiWOLDc2I8wKUK";

enum {
    GOVEE_H5059_LEAK_UNKNOWN = -1,
    GOVEE_H5059_LEAK_DRY     = 0,
    GOVEE_H5059_LEAK_WET     = 1,
};

/**
Govee Water Leak Detector H5059, also H5830.

The H5830 (leak detector with a fixed cable probe) uses the same radio protocol and is
decoded here too; dec[9] tells the two apart (reported as `product`).

Modulation, timing, and transmission:

The device uses FSK pulse PCM encoding.
- Demod mode in rtl_433 is FSK_PULSE_PCM.
- short_width = 100 us and long_width = 100 us (PCM symbol timing).
- reset_limit = 2000 us.
- Captures show event packets sent as short bursts with repeated transmissions.

Sensor uplink frame format:

    2C 4C 4A LL SS [ciphertext...] CC CC

- 2C 4C 4A: sync word
- LL: outer frame length in bytes (SS + ciphertext + CRC)
- SS: XOR stream start offset (seed)
- ciphertext: encrypted payload bytes
- CC CC: CRC-16/AUG-CCITT, poly=0x1021, init=0x1d0f

Decrypted payload (19+ bytes):

    MM II II II II [payload...]

- dec[0]: message class (0x11 = telemetry, 0x01 = pairing, 0x02 = other)
- dec[1-4]: 32-bit device ID in wire byte order
- dec[5-8]: big-endian Unix time from the sensor's clock, set by the gateway (FFFFFFFF after
    power-up until the gateway syncs it; stops advancing on sensors that lost their gateway)
- dec[9]: product code, constant per device and unchanged by a battery swap
    (0x1e = H5059 on 11 units, 0x13 = H5830 on 2 units)
- dec[10]: unknown, 0x14 on all units seen
- dec[11]: battery level in percent (telemetry frames)
- dec[12]: signed RSSI in dBm of the last packet the sensor received from the gateway
    (0 after power-up; varies widely from packet to packet)
- dec[13]: subtype (0x00 = power on, 0x05 = button, 0x06 = leak, 0x07 = post-alarm)
- dec[14]: top probe wet flag (0x01 = top probe pair detects water)
- dec[15]: bottom probe wet flag (0x01 = bottom probe pair detects water)
- dec[16]: unknown (0x03 in every H5830 frame seen)
- dec[17]: alarm-active flag (non-zero while either probe pair is wet; seen as
    0x01 on the initial leak frame and 0x02 on a follow-up frame ~1s later)
- dec[18+]: unknown/variable payload

Event mapping:

- msg_class 0x11 + subtype 0x05: Button Press
- msg_class 0x11 + subtype 0x06 + dec[17] != 0 + (dec[14] == 0x01 or dec[15] == 0x01): Water Leak
- msg_class 0x11 + subtype 0x07: Post Alarm
- msg_class 0x11 + subtype 0x00: Power On (sent when batteries are inserted)
- msg_class 0x01: Pairing
- msg_class 0x02: Class 0x02

Battery level (observed 2026-11-06):

- dec[11] read 100 on 12 units with fresh cells (~3.0 V). Swapping 2.4 V used cells into a
    paired unit changed it to 0x26 (38) in both the power-on and the following button frames,
    while the Govee app showed 2 of 4 battery bars. Reported as battery_ok (0..1) + battery_pct.

Top/bottom probe flags (confirmed 2026-06-14):

- A bottom-probe-only leak produced dec[14]=0x00, dec[15]=0x01, dec[17]=0x01.
- A top-probe-only leak produced dec[14]=0x01, dec[15]=0x00, dec[17]=0x01.
- A simultaneous top+bottom leak's first frame matched the top-only pattern
    (dec[14]=0x01, dec[15]=0x00); a follow-up subtype-0x08 frame ~1s later showed
    both dec[14]=0x01 and dec[15]=0x01.
- The original logic required dec[15] == 0x01 AND dec[17] == 0x01, which only
    matched the bottom-probe case -- a top-probe-only leak fell through to the
    generic "Telemetry" event and was never reported as "Water Leak". This is
    fixed by checking dec[17] != 0 and either probe flag, and the specific probe
    (top/bottom) is now reported via the leak_top/leak_bottom fields.

H5830 (observed 2026-11-07, 2 units, both with dec[9]=0x13):

- The H5830 has bottom contacts on the base plus a fixed cable probe with top and bottom
    contacts. All three report as one input: wetting each one alone gave identical frames
    (dec[13..18] = 06 00 01 03 01 80), i.e. leak_bottom=1, leak_top=0. The Govee app also
    only shows "Bottom Contact / Cord wet".
- Button, Water Leak, Post Alarm (~2 s after the leak frame), battery and the gateway
    clock/RSSI bytes behave as on the H5059; no frame is sent when the contacts dry.

Subtype 0x07 note:

- Observed following leak-active (0x06) frames, but the exact semantics are not
    confirmed. Known triggers include the sensor recovering naturally (probe dries),
    a manual alarm reset via the device button, and alarm suppression (which silences
    the alert for ~1 hour while the probe may still be wet). Because of the
    suppression case, subtype 0x07 cannot be interpreted as a "dry/recovered" signal,
    so `detect_wet` is not emitted for this event type.

Pairing mode notes (reverse-engineering observations):

- Holding the leak detector button for about 3 seconds puts it into pairing mode
  and it emits msg_class 0x01 pairing/setup frames.
- Gateway bind generation tooling uses:

    preamble = AAAAAAAAAAAAAAAA
    sync     = 2C 4C 4A

- Expected gateway-to-sensor bind cleartext body (before XOR encryption), as used
  by generate_bind.py:

    cmd gwid[4] pad[4] mac[6] token[16] tail[2]

  where:
  - cmd is 0x01 (bind command)
  - gwid is gateway ID (4 bytes)
  - mac is sensor MAC (6 bytes)
  - token is a separate 16-byte field (bind/session token)
  - tail is 2 bytes padding/terminator
- In the decrypted bind payload layout used by tooling, token bytes are expected at
  offset 16..31 and MAC at offset 10..15.
- This decoder does not decode gateway transmit packets; notes above document the
  expected bind payload structure for companion tooling and future work.

Unpaired ID behavior:

- ID_wire of FFFFFFFF has been treated as "unpaired" in companion analysis tools.
- Observed behavior note: when ID is FFFFFFFF, normal sensor RF telemetry may be
    suppressed until a valid bind is completed. For example, when a leak is detected
    while the device is unpaired, no RF message is emitted until the device is paired,
    and only the audible alarm is emitted at the time of the leak event. After pairing,
    the leak event is emitted with the assigned ID.
*/
static int govee_h5059_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t frame[GOVEE_H5059_MAX_FRAME];
    uint8_t dec[64];

    int row           = -1;
    unsigned sync_pos = 0;

    // Stage 1: find a row that contains either the native sync or the known skewed sync.
    for (int r = 0; r < bitbuffer->num_rows; ++r) {
        if (bitbuffer->bits_per_row[r] < 8 * GOVEE_H5059_MIN_FRAME) {
            continue;
        }

        unsigned pos = bitbuffer_search(bitbuffer, r, 0, govee_h5059_sync, GOVEE_H5059_SYNC_LEN * 8);
        if (pos < bitbuffer->bits_per_row[r]) {
            row      = r;
            sync_pos = pos;
            break;
        }

        // The device preamble is MSB-first; depending on demod lock, bytes can
        // arrive shifted by one bit, turning 2c 4c 4a into 16 26 25.
        unsigned skew_pos = bitbuffer_search(bitbuffer, r, 0, govee_h5059_sync_skew1, GOVEE_H5059_SYNC_LEN * 8);
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
    sync_pos += GOVEE_H5059_SYNC_LEN * 8;

    unsigned bits_after_sync = bitbuffer->bits_per_row[row] - sync_pos;
    if (bits_after_sync < 8 * 4) {
        return DECODE_ABORT_LENGTH;
    }

    unsigned bytes_after_sync = bits_after_sync / 8;
    if (bytes_after_sync > GOVEE_H5059_MAX_FRAME) {
        bytes_after_sync = GOVEE_H5059_MAX_FRAME;
    }

    bitbuffer_extract_bytes(bitbuffer, row, sync_pos, frame, bytes_after_sync * 8);

    uint8_t outer_len = frame[0];
    if (outer_len < 4 || outer_len > GOVEE_H5059_MAX_FRAME - 1) {
        return DECODE_FAIL_SANITY;
    }

    if (bytes_after_sync < (unsigned)(1 + outer_len)) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t seed      = frame[1];
    unsigned enc_len  = outer_len - 3;
    unsigned crc_offs = 2 + enc_len;

    if (enc_len < 8 || enc_len > sizeof(dec)) {
        return DECODE_FAIL_SANITY;
    }

    uint16_t crc_calc = crc16(&frame[2], enc_len, GOVEE_H5059_CRC_POLY, GOVEE_H5059_CRC_INIT);
    uint16_t crc_recv = ((uint16_t)frame[crc_offs] << 8) | frame[crc_offs + 1];
    if (crc_calc != crc_recv) {
        return DECODE_FAIL_MIC;
    }

    for (unsigned i = 0; i < enc_len; ++i) {
        dec[i] = frame[2 + i] ^ govee_h5059_key[(i + seed) % GOVEE_H5059_KEY_LEN];
    }

    if (enc_len < GOVEE_H5059_MIN_DECRYPTED_LEN) {
        return DECODE_FAIL_SANITY;
    }

    uint8_t msg_class = dec[GOVEE_H5059_MSG_CLASS_OFFSET];
    uint32_t id_wire  = ((uint32_t)dec[GOVEE_H5059_ID_OFFSET] << 24) |
                        ((uint32_t)dec[GOVEE_H5059_ID_OFFSET + 1] << 16) |
                        ((uint32_t)dec[GOVEE_H5059_ID_OFFSET + 2] << 8) |
                        dec[GOVEE_H5059_ID_OFFSET + 3];
    // The app-facing/canonical ID uses swapped 16-bit words relative to wire order.
    uint32_t id = ((id_wire & 0xffff) << 16) | ((id_wire >> 16) & 0xffff);
    char id_str[9];
    char id_wire_str[9];
    snprintf(id_str, sizeof(id_str), "%08x", (unsigned)id);
    snprintf(id_wire_str, sizeof(id_wire_str), "%08x", (unsigned)id_wire);

    int subtype     = enc_len > GOVEE_H5059_SUBTYPE_OFFSET ? dec[GOVEE_H5059_SUBTYPE_OFFSET] : -1;
    int leak_top    = enc_len > GOVEE_H5059_LEAK_TOP_OFFSET ? dec[GOVEE_H5059_LEAK_TOP_OFFSET] : -1;
    int leak_bottom = enc_len > GOVEE_H5059_LEAK_BOTTOM_OFFSET ? dec[GOVEE_H5059_LEAK_BOTTOM_OFFSET] : -1;
    int leak_alarm  = enc_len > GOVEE_H5059_LEAK_ALARM_OFFSET ? dec[GOVEE_H5059_LEAK_ALARM_OFFSET] : -1;
    int leak_status = GOVEE_H5059_LEAK_UNKNOWN;
    int battery_pct = -1;
    char product[8] = "";

    char const *event = "Unknown";
    if (msg_class == GOVEE_H5059_MSG_CLASS_TELEMETRY) {
        event = "Telemetry";
        uint8_t product_code = dec[GOVEE_H5059_PRODUCT_OFFSET];
        if (product_code == GOVEE_H5059_PRODUCT_H5059) {
            snprintf(product, sizeof(product), "H5059");
        }
        else if (product_code == GOVEE_H5059_PRODUCT_H5830) {
            snprintf(product, sizeof(product), "H5830");
        }
        else {
            snprintf(product, sizeof(product), "0x%02x", product_code);
        }
        if (dec[GOVEE_H5059_BATTERY_OFFSET] <= 100) {
            battery_pct = dec[GOVEE_H5059_BATTERY_OFFSET];
        }
        if (subtype == GOVEE_H5059_SUBTYPE_POWER_ON) {
            event = "Power On";
        }
        else if (subtype == GOVEE_H5059_SUBTYPE_BUTTON) {
            event       = "Button Press";
            leak_status = GOVEE_H5059_LEAK_DRY;
        }
        else if (subtype == GOVEE_H5059_SUBTYPE_LEAK && leak_alarm != 0 && (leak_top == GOVEE_H5059_LEAK_FLAG_ACTIVE || leak_bottom == GOVEE_H5059_LEAK_FLAG_ACTIVE)) {
            event       = "Water Leak";
            leak_status = GOVEE_H5059_LEAK_WET;
        }
        else if (subtype == GOVEE_H5059_SUBTYPE_POST_ALARM) {
            event = "Post Alarm";
        }
    }
    else if (msg_class == GOVEE_H5059_MSG_CLASS_PAIRING) {
        event = "Pairing";
    }
    else if (msg_class == GOVEE_H5059_MSG_CLASS_OTHER) {
        event = "Class 0x02";
    }
    else {
        return DECODE_ABORT_EARLY;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",                 DATA_STRING, "Govee-H5059",
            "id",           "",                 DATA_STRING, id_str,
            "id_wire",      "",                 DATA_STRING, id_wire_str,
            "product",      "Product",          DATA_COND,   product[0] != '\0', DATA_STRING, product,
            "event",       "",                 DATA_STRING, event,
            "msg_class",    "",                 DATA_FORMAT, "0x%02x", DATA_INT, msg_class,
            "subtype",      "",                 DATA_COND,   subtype >= 0, DATA_FORMAT, "0x%02x", DATA_INT, subtype,
            "detect_wet",   "",                 DATA_COND,   leak_status >= 0, DATA_INT, leak_status,
            "leak_top",     "",                 DATA_COND,   leak_status == GOVEE_H5059_LEAK_WET, DATA_INT, leak_top == GOVEE_H5059_LEAK_FLAG_ACTIVE,
            "leak_bottom",  "",                 DATA_COND,   leak_status == GOVEE_H5059_LEAK_WET, DATA_INT, leak_bottom == GOVEE_H5059_LEAK_FLAG_ACTIVE,
            "battery_ok",   "Battery level",    DATA_COND,   battery_pct >= 0, DATA_DOUBLE, battery_pct * 0.01,
            "battery_pct",  "Battery level",    DATA_COND,   battery_pct >= 0, DATA_FORMAT, "%d %%", DATA_INT, battery_pct,
            "mic",          "Integrity",        DATA_STRING, "CRC",
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
        "product",
        "event",
        "msg_class",
        "subtype",
        "detect_wet",
        "leak_top",
        "leak_bottom",
        "battery_ok",
        "battery_pct",
        "mic",
        NULL,
};

/* Device registration and demod timing profile. */
r_device const govee_h5059 = {
        .name        = "Govee Water Leak Detector H5059",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 2000,
        .decode_fn   = &govee_h5059_decode,
        .fields      = output_fields,
        .priority    = 10, // H5310 shares this family's framing and runs at higher
                           // priority; H5310 claims temp/periodic/status/ping frames
                           // first; H5059 only sees what H5310 passes.
};
