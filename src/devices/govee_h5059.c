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
#define GOVEE_H5059_SUBTYPE_OFFSET      13
#define GOVEE_H5059_LEAK_FLAG1_OFFSET   15
#define GOVEE_H5059_LEAK_FLAG2_OFFSET   17

#define GOVEE_H5059_LEAK_FLAG_ACTIVE    0x01

#define GOVEE_H5059_MSG_CLASS_TELEMETRY 0x11
#define GOVEE_H5059_MSG_CLASS_PAIRING   0x01
#define GOVEE_H5059_MSG_CLASS_OTHER     0x02

#define GOVEE_H5059_SUBTYPE_BUTTON      0x05
#define GOVEE_H5059_SUBTYPE_LEAK        0x06
#define GOVEE_H5059_SUBTYPE_POST_ALARM  0x07

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
Govee Water Leak Detector H5059.

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
- dec[5-12]: unknown/reserved bytes
- dec[13]: subtype (0x05 = button, 0x06 = leak, 0x07 = post-alarm)
- dec[14]: unknown
- dec[15]: state flag (checked with dec[17] for leak)
- dec[16]: unknown
- dec[17]: state flag (checked with dec[15] for leak)
- dec[18+]: unknown/variable payload

Event mapping:

- msg_class 0x11 + subtype 0x05: Button Press
- msg_class 0x11 + subtype 0x06 + dec[15] == 0x01 + dec[17] == 0x01: Water Leak
- msg_class 0x11 + subtype 0x07: Post Alarm
- msg_class 0x01: Pairing
- msg_class 0x02: Class 0x02

Subtype 0x07 note:

- Field observations indicate 0x07 commonly follows leak-active (0x06) frames and
    likely represents dry/recovery state.
- This is still marked as provisional and should be confirmed by correlating RF
    captures with Govee gateway/app state transitions.

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

    int subtype     = enc_len > GOVEE_H5059_SUBTYPE_OFFSET ? dec[GOVEE_H5059_SUBTYPE_OFFSET] : -1;
    int leak_flag_1 = enc_len > GOVEE_H5059_LEAK_FLAG1_OFFSET ? dec[GOVEE_H5059_LEAK_FLAG1_OFFSET] : -1;
    int leak_flag_2 = enc_len > GOVEE_H5059_LEAK_FLAG2_OFFSET ? dec[GOVEE_H5059_LEAK_FLAG2_OFFSET] : -1;
    int leak_status = GOVEE_H5059_LEAK_UNKNOWN;

    char const *event = "Unknown";
    if (msg_class == GOVEE_H5059_MSG_CLASS_TELEMETRY) {
        event = "Telemetry";
        if (subtype == GOVEE_H5059_SUBTYPE_BUTTON) {
            event       = "Button Press";
            leak_status = GOVEE_H5059_LEAK_DRY;
        }
        else if (subtype == GOVEE_H5059_SUBTYPE_LEAK && leak_flag_1 == GOVEE_H5059_LEAK_FLAG_ACTIVE && leak_flag_2 == GOVEE_H5059_LEAK_FLAG_ACTIVE) {
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

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",                 DATA_STRING, "Govee-H5059",
            "id",           "",                 DATA_FORMAT, "%08x", DATA_INT, id,
            "id_wire",      "",                 DATA_FORMAT, "%08x", DATA_INT, id_wire,
            "event",        "",                 DATA_STRING, event,
            "msg_class",    "",                 DATA_FORMAT, "0x%02x", DATA_INT, msg_class,
            "subtype",      "",                 DATA_COND,   subtype >= 0, DATA_FORMAT, "0x%02x", DATA_INT, subtype,
            "detect_wet",   "",                 DATA_COND,   leak_status >= 0, DATA_INT, leak_status,
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
        "event",
        "msg_class",
        "subtype",
        "detect_wet",
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
};
