/** @file
    Govee Dual-Probe Thermometer H5112.

    Copyright (C) 2026 Paul Antaki (\@Pablo1)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define GOVEE_H5112_SYNC_LEN  3
#define GOVEE_H5112_MIN_FRAME 7
#define GOVEE_H5112_MAX_FRAME 128
#define GOVEE_H5112_KEY_LEN   128

#define GOVEE_H5112_CRC_POLY  0x1021
#define GOVEE_H5112_CRC_INIT  0x1d0f

// msg_class values this decoder handles, with their fixed decrypted
// payload lengths.
#define GOVEE_H5112_MSG_PERIODIC      0x13 // autonomous periodic report (~10 min)
#define GOVEE_H5112_PERIODIC_DEC_LEN  57
#define GOVEE_H5112_MSG_TRIGGERED     0x71 // gateway-triggered status response
#define GOVEE_H5112_TRIGGERED_DEC_LEN 28

// Minimum decrypted bytes needed to read all sensor fields (through dec[9]).
#define GOVEE_H5112_MIN_DEC 10

// Byte offsets in the decrypted payload.
#define GOVEE_H5112_MSG_CLASS_OFFSET 0
#define GOVEE_H5112_ID_OFFSET        1  // 4 bytes (big-endian wire order)
#define GOVEE_H5112_BATTERY_OFFSET   5  // battery percent (0x64 = 100%)
#define GOVEE_H5112_SENSOR_OFFSET    6  // 4-byte packed sensor word, dec[6..9]

// dec[6..9] is one 32-bit little-endian word packing both probe temperatures
// and humidity, written by the device's firmware sensor-packing routine.
// Field layout (bit positions within the 32-bit word):
//   bits [0:10]  (11 bits) = probe 2 temperature
//   bits [11:21] (11 bits) = probe 1 temperature
//   bits [22:31] (10 bits) = humidity
// Both temperature fields use the same scale/bias: count = (T_C + 40) * 10,
// i.e. 0.1 °C resolution, zero-anchored at -40 °C. Humidity: count = RH% * 10.
// Firmware-verified bit-exact: forward-encoding real app-displayed readings
// reproduces the real RF bytes exactly, and decoding the in-frame history
// buffer with this layout (no app data needed) traces a smooth temperature
// curve through real wrap-boundary crossings -- see project notes.
#define GOVEE_H5112_FIELD_SCALE   10.0f  // counts per °C, and per % RH
#define GOVEE_H5112_TEMP_BIAS_C   40.0f  // °C added to zero-anchor the temp fields
#define GOVEE_H5112_PROBE2_MASK   0x7ffu
#define GOVEE_H5112_PROBE1_SHIFT  11
#define GOVEE_H5112_PROBE1_MASK   0x7ffu
#define GOVEE_H5112_HUMID_SHIFT   22
#define GOVEE_H5112_HUMID_MASK    0x3ffu

// History buffer: dec[17-56], 10 × 4-byte records of [d6, d7, d8, d9],
// sampled at ~1-minute intervals within the 10-minute reporting window,
// oldest record first (dec[17-20] oldest, dec[53-56] most recent). Each
// record uses the same packed-word layout as the live dec[6..9] fields.
#define GOVEE_H5112_HISTORY_OFFSET 17
#define GOVEE_H5112_HISTORY_COUNT  10

static uint8_t const govee_h5112_sync[]       = {0x2c, 0x4c, 0x4a};
static uint8_t const govee_h5112_sync_skew1[] = {0x16, 0x26, 0x25};
static uint8_t const govee_h5112_key[GOVEE_H5112_KEY_LEN + 1] =
        "s6amyEvO8UslCY0eZjgc2S6APCVLgLxzFvL2Z5GWPW7fKVjy2oAU6uiKU3lZCHm62VYQQuCtgxzPgGd8UDRPVZpDRAsh5EdYq1E4j4morJ3vd6tWx8BiWOLDc2I8wKUK";

/**
Govee Dual-Probe Thermometer H5112.

Based on the Govee H5059 decoder by Reece Neff <reeceneff@gmail.com>
(rtl_433 PR #3493), which reverse-engineered the shared Govee FSK
protocol used by this device family -- the 2c 4c 4a sync words, the
128-byte XOR key, and the CRC-16/AUG-CCITT framing. That protocol
foundation is reused here unchanged.

The H5112 is a general-purpose dual-probe thermometer. Both probes cover
the full device range (documented upper bound 140 °F / 60 °C; no lower
bound specified). Probe 1 includes a humidity sensor; probe 2 is
temperature-only. Typical deployments include refrigerators, freezers,
wine cellars, greenhouses, and other environments.

Modulation, timing, and transmission:

The device uses FSK pulse PCM encoding, sharing the same sync words,
XOR key, and CRC-16/AUG-CCITT framing as the Govee H5059 water leak
detector and the H5310 pool/spa thermometer. Those shared protocol
details were reverse-engineered by Reece Neff for the H5059 (PR #3493).
- Demod mode in rtl_433 is FSK_PULSE_PCM.
- short_width = 100 us and long_width = 100 us (PCM symbol timing).
- reset_limit = 2000 us.
- The sensor transmits autonomously approximately every 10 minutes.
- All temperature and humidity data is carried on the 912 MHz Sub-1G uplink;
  the companion app updates only when an RF frame is received.

Sensor uplink frame format (common to the Govee FSK device family):

    2C 4C 4A LL SS [ciphertext...] CC CC

- 2C 4C 4A: sync word
- LL: outer frame length in bytes (SS + ciphertext + CRC)
- SS: XOR stream start offset (seed)
- ciphertext: encrypted payload bytes
- CC CC: CRC-16/AUG-CCITT, poly=0x1021, init=0x1d0f

Periodic sensor report (msg_class 0x13), 57-byte decrypted payload.
This is the device's autonomous broadcast, transmitted every ~10 minutes.

    byte:   0    1  2  3  4   5    6  7  8  9   10  11   12  13    14-16   17-56
    field:  13  <id_wire 4B>  bat  <sensor word, 4B>  flags  00  <time_s LE>  const  <history>

Triggered status response (msg_class 0x71), 28-byte decrypted payload.
Sent in response to a gateway poll (msg_class 0x70 ping). Contains the
same sensor fields as 0x13 in the same byte positions, but omits the
history buffer, seconds counter, and constant marker bytes.

    byte:   0    1  2  3  4   5    6  7  8  9    10  11  12  13
    field:  71  <id_wire 4B>  bat  <sensor word, 4B>  ?   00  <session_id LE>

- dec[0]:  0x13 or 0x71, msg_class (identifies this frame type)
- dec[1-4]: device ID in wire byte order (big-endian); lower 16 bits are a
    pairing token shared by all devices bound to the same gateway
- dec[5]:  battery percent (0x64 = 100%)
- dec[6-9]: one 32-bit little-endian packed sensor word (see below) holding
    probe 2 temperature, probe 1 temperature, and humidity. This matches the
    bit-packing of the firmware's own sensor-encoding routine, confirmed by
    disassembling a dumped firmware image -- not just calibration-fitted from
    RF captures.
- dec[10]: status/flags byte. bit[7] = display unit (0=°C, 1=°F), confirmed from
    gateway unit-change command responses. bit[6] = config/state flag of
    undetermined meaning; usually differs between units (one unit sets it, another
    clears it) but it is NOT a fixed per-device constant -- one unit was observed
    with it both set and clear. Verified NOT correlated with probe temperature
    (checked over 86 frames spanning probe 1 +0.4..+25.9 °C). bits[5:0] = 0 in
    all captures. NOT a probe-swap flag (the two probe connectors are physically
    different).
- dec[11]: 0x00 (reserved or padding; full byte unused across all captures)
- dec[12-13]: seconds counter, little-endian 16-bit, increments at 1 Hz
- dec[14-15]: 0x37 0x6a (constant marker bytes, observed stable across all captures;
    earlier firmware builds showed 0x36 0x6a)
- dec[16]: history record count; normally 10 (0x0a), less on a freshly powered device
- dec[17-56]: rolling history buffer -- 10 x 4-byte records (oldest first), each
    record is a packed sensor word in the same layout as dec[6-9]

Sensor word derivation (dec[6..9], and each 4-byte history record):

The firmware packs both probe temperatures and humidity into one 32-bit
little-endian word, written verbatim as 4 consecutive bytes:

    packed = dec[6] | (dec[7] << 8) | (dec[8] << 16) | (dec[9] << 24)
    probe2_field   =  packed        & 0x7ff   // bits [0:10],  11 bits
    probe1_field   = (packed >> 11) & 0x7ff   // bits [11:21], 11 bits
    humidity_field = (packed >> 22) & 0x3ff   // bits [22:31], 10 bits

    temperature_2_C = probe2_field / 10.0 - 40.0
    temperature_C   = probe1_field / 10.0 - 40.0
    humidity        = humidity_field / 10.0

Both temperature fields use the identical scale/bias (0.1 °C/count,
zero-anchored at -40 °C) -- probe 2 is a direct absolute reading, not a
modular counter; no per-device wrap-count state is needed or maintained.
Equivalently, byte-by-byte without reassembling the 32-bit word:

    dec[6]        = probe2 bits [0:7]
    dec[7] & 0x07 = probe2 bits [8:10]
    dec[7] >> 3   = probe1 bits [0:4]
    dec[8] & 0x3f = probe1 bits [5:10]
    dec[8] >> 6   = humidity bits [0:1]
    dec[9]        = humidity bits [2:9]

This formula is firmware-verified bit-exact, not just calibration-fitted:
the device's firmware (FR8016HA, dumped via UART bootloader exploit and
disassembled) contains this exact symmetric 11/11/10-bit packing function.
Confirmed two independent ways: (1) forward-encoding real app-displayed
temperature/humidity readings reproduces the real RF bytes exactly across
multiple frames, multiple devices, and multiple probe-2 absolute ranges;
(2) decoding the in-frame history buffer with this formula (no app data
involved at all) traces a smooth, physically continuous temperature curve
through real-world temperature excursions, including 25.6 °C wrap-boundary
crossings that an earlier modular-plus-tracked-wrap-count model could not
represent without an external wrap counter.

Verified examples (real captured dec[] bytes vs. confirmed app readings):
- dec[6-9] = 0xde 0x9a 0x56 0xd0: probe2=33.4 °C, probe1=32.3 °C, hum=83.3 %
  (app: 33.4 / 32.3 / 83.3 -- exact)
- dec[6-9] = 0x9c 0x18 0x0d 0xa0: probe2=-24.4 °C, probe1=1.9 °C, hum=64.0 %
  (app: -24.4 / 1.9 / 64.0 -- exact; probe2 well below the 25.6 °C wrap period,
  read directly with no wrap-count tracking)

Device ID:

`id_wire` is the full 32-bit on-wire device ID (e.g. 0x0b279cb2); `id`
is the same value with its two 16-bit half-words swapped (0x9cb20b27),
following the convention of the Govee H5059 and H5310 decoders. The low
16 bits of id_wire are a pairing token shared by all devices bound to
the same gateway (0x9cb2 in all observed captures).

Priority and coexistence:

This decoder runs at priority 5, after the H5310 decoder (priority 0,
which claims LL=0x10/0x1f/0x3d frames first) but before the H5059
decoder (priority 10). Frames are claimed by checking dec[0] == 0x13 or
0x71 post-decryption, plus each frame type's fixed payload length.

The only frame shape shared with another decoded device is the 0x71
status reply, which the H5310 pool/spa thermometer also uses at the same
payload length. That collision is resolved structurally from both sides:
H5310 requires its constant 0xff at dec[9] (a value this decoder's
humidity field can never produce, since it would mean more than 100% RH),
and this decoder rejects any frame whose humidity decodes above 100%
(which every genuine H5310 status frame does, at 102.3%). Priority
ordering is therefore only a belt-and-braces measure for the
temperature-bearing frames, not the actual differentiator.

Gateway and pairing frames -- observed but not decoded by this decoder:

All gateway-originated frames share the same outer header:
  dec[1-2] = gateway pairing token (low 16 bits of all device IDs on gateway)
  dec[3-4] = target device high-word ID
  dec[5-6] = 0x6A 0x37 (constant across all captures)
  dec[7-8] = gateway uptime counter, big-endian 16-bit, ~100 ms ticks

  msg_class 0x70 (gateway → device): command frame. dec[9]=command,
    dec[10]=payload. Device always responds with 0x71 echoing dec[9-10] in
    its own dec[12-13]. Known commands: 0x35=network test/poll, 0x02=set
    unit (0x00=°C / 0x01=°F), 0x17=gateway alerts (0x00=off / 0x01=on).
    Always transmitted twice for RF reliability.

  msg_class 0x36 (gateway → device): alarm settings push. Device responds
    with 0x71 where dec[12-13] = {0x36, 0x00}. Payload not decoded.

  msg_class 0x07 (gateway → device): pairing invitation. Gateway proposes
    a new random high-word device ID in dec[3-4]; the full new id_wire is
    {dec[3]:02x}{dec[4]:02x} + pairing_token. Two frames with different
    proposed IDs are burst-transmitted simultaneously; device accepts both.

  msg_class 0x08 (gateway → device): delete/unpair. Removes the device
    from the gateway; device ceases transmitting periodic frames.

  msg_class 0x11 (device → gateway): pairing response. Sent in reply to
    0x07. dec[1-4] = the proposed id_wire being accepted; dec[5] = subtype
    (0x01 in pairing context); dec[6-10] = same sensor fields as 0x71.
*/
static int govee_h5112_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t frame[GOVEE_H5112_MAX_FRAME];
    uint8_t dec[GOVEE_H5112_MAX_FRAME];

    int row           = -1;
    unsigned sync_pos = 0;

    // Stage 1: find a row that contains either the native sync or the known skewed sync.
    for (int r = 0; r < bitbuffer->num_rows; ++r) {
        if (bitbuffer->bits_per_row[r] < 8 * GOVEE_H5112_MIN_FRAME) {
            continue;
        }

        unsigned pos = bitbuffer_search(bitbuffer, r, 0, govee_h5112_sync, GOVEE_H5112_SYNC_LEN * 8);
        if (pos < bitbuffer->bits_per_row[r]) {
            row      = r;
            sync_pos = pos;
            break;
        }

        // The device preamble is MSB-first; depending on demod lock, bytes can
        // arrive shifted by one bit, turning 2c 4c 4a into 16 26 25.
        unsigned skew_pos = bitbuffer_search(bitbuffer, r, 0, govee_h5112_sync_skew1, GOVEE_H5112_SYNC_LEN * 8);
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
    sync_pos += GOVEE_H5112_SYNC_LEN * 8;

    unsigned bits_after_sync = bitbuffer->bits_per_row[row] - sync_pos;
    if (bits_after_sync < 8 * 4) {
        return DECODE_ABORT_LENGTH;
    }

    unsigned bytes_after_sync = bits_after_sync / 8;
    if (bytes_after_sync > GOVEE_H5112_MAX_FRAME) {
        bytes_after_sync = GOVEE_H5112_MAX_FRAME;
    }

    bitbuffer_extract_bytes(bitbuffer, row, sync_pos, frame, bytes_after_sync * 8);

    uint8_t outer_len = frame[0];
    if (outer_len < 4 || outer_len > GOVEE_H5112_MAX_FRAME - 1) {
        return DECODE_FAIL_SANITY;
    }

    if (bytes_after_sync < (unsigned)(1 + outer_len)) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t seed      = frame[1];
    unsigned enc_len  = outer_len - 3;
    unsigned crc_offs = 2 + enc_len;

    if (enc_len < GOVEE_H5112_MIN_DEC || enc_len > sizeof(dec)) {
        return DECODE_ABORT_EARLY;
    }

    uint16_t crc_calc = crc16(&frame[2], enc_len, GOVEE_H5112_CRC_POLY, GOVEE_H5112_CRC_INIT);
    uint16_t crc_recv = ((uint16_t)frame[crc_offs] << 8) | frame[crc_offs + 1];
    if (crc_calc != crc_recv) {
        return DECODE_FAIL_MIC;
    }

    for (unsigned i = 0; i < enc_len; ++i) {
        dec[i] = frame[2 + i] ^ govee_h5112_key[(i + seed) % GOVEE_H5112_KEY_LEN];
    }

    uint8_t msg_class = dec[GOVEE_H5112_MSG_CLASS_OFFSET];
    if (msg_class != GOVEE_H5112_MSG_PERIODIC && msg_class != GOVEE_H5112_MSG_TRIGGERED) {
        decoder_logf(decoder, 1, __func__, "unknown msg_class 0x%02x", msg_class);
        decoder_log_bitrow(decoder, 1, __func__, dec, enc_len * 8, "unknown frame dec[] bytes");
        return DECODE_ABORT_EARLY;
    }

    // Both recognized frame types have a fixed payload length. Other Govee
    // family devices share this outer envelope and could reuse these
    // msg_class values with a different layout, so reject any length
    // mismatch instead of guessing at the payload.
    if ((msg_class == GOVEE_H5112_MSG_PERIODIC && enc_len != GOVEE_H5112_PERIODIC_DEC_LEN)
            || (msg_class == GOVEE_H5112_MSG_TRIGGERED && enc_len != GOVEE_H5112_TRIGGERED_DEC_LEN)) {
        decoder_logf(decoder, 1, __func__, "msg_class 0x%02x with unexpected length %u", msg_class, enc_len);
        return DECODE_ABORT_EARLY;
    }

    uint32_t id_wire = ((uint32_t)dec[GOVEE_H5112_ID_OFFSET] << 24)
            | ((uint32_t)dec[GOVEE_H5112_ID_OFFSET + 1] << 16)
            | ((uint32_t)dec[GOVEE_H5112_ID_OFFSET + 2] << 8)
            | dec[GOVEE_H5112_ID_OFFSET + 3];
    // Canonical ID: two 16-bit half-words swapped, matching govee_h5059/h5310.
    uint32_t id = ((id_wire & 0xffff) << 16) | ((id_wire >> 16) & 0xffff);
    char id_str[9];
    char id_wire_str[9];
    snprintf(id_str, sizeof(id_str), "%08x", (unsigned)id);
    snprintf(id_wire_str, sizeof(id_wire_str), "%08x", (unsigned)id_wire);

    int battery_pct = dec[GOVEE_H5112_BATTERY_OFFSET];

    // Unpack the live sensor word (dec[6..9]); see the "Sensor word
    // derivation" doc comment above for the bit layout.
    uint32_t packed = (uint32_t)dec[GOVEE_H5112_SENSOR_OFFSET]
            | ((uint32_t)dec[GOVEE_H5112_SENSOR_OFFSET + 1] << 8)
            | ((uint32_t)dec[GOVEE_H5112_SENSOR_OFFSET + 2] << 16)
            | ((uint32_t)dec[GOVEE_H5112_SENSOR_OFFSET + 3] << 24);
    uint32_t probe2_field = packed & GOVEE_H5112_PROBE2_MASK;
    uint32_t probe1_field = (packed >> GOVEE_H5112_PROBE1_SHIFT) & GOVEE_H5112_PROBE1_MASK;
    uint32_t humid_field  = (packed >> GOVEE_H5112_HUMID_SHIFT) & GOVEE_H5112_HUMID_MASK;
    float probe1_c  = probe1_field / GOVEE_H5112_FIELD_SCALE - GOVEE_H5112_TEMP_BIAS_C;
    float probe2_c  = probe2_field / GOVEE_H5112_FIELD_SCALE - GOVEE_H5112_TEMP_BIAS_C;
    float humidity  = humid_field / GOVEE_H5112_FIELD_SCALE;

    // Humidity above 100% RH is physically impossible for this sensor. In
    // particular, a Govee-H5310 status reply shares this frame's msg_class
    // (0x71) and payload length, and its constant 0xcc 0xff at dec[8-9]
    // always decodes to 102.3% here -- this guard rejects such frames
    // structurally (the mirror of H5310's own dec[9] == 0xff check) rather
    // than relying on decoder priority ordering alone.
    if (humidity > 100.0f) {
        return DECODE_FAIL_SANITY;
    }

    // History buffer: 10 × 4-byte records, oldest first, each a packed sensor
    // word in the same layout as dec[6..9]. Stateless -- no wrap-count
    // tracking needed; probe 2 is read as a direct absolute value here too.
    double hist_t1[GOVEE_H5112_HISTORY_COUNT];
    double hist_t2[GOVEE_H5112_HISTORY_COUNT];
    double hist_hum[GOVEE_H5112_HISTORY_COUNT];
    int has_history = (msg_class == GOVEE_H5112_MSG_PERIODIC)
            && (enc_len >= GOVEE_H5112_HISTORY_OFFSET + GOVEE_H5112_HISTORY_COUNT * 4);
    if (has_history) {
        for (int i = 0; i < GOVEE_H5112_HISTORY_COUNT; i++) {
            unsigned base = GOVEE_H5112_HISTORY_OFFSET + (unsigned)i * 4;
            uint32_t hist_packed = (uint32_t)dec[base]
                    | ((uint32_t)dec[base + 1] << 8)
                    | ((uint32_t)dec[base + 2] << 16)
                    | ((uint32_t)dec[base + 3] << 24);
            uint32_t hist_probe2_field = hist_packed & GOVEE_H5112_PROBE2_MASK;
            uint32_t hist_probe1_field = (hist_packed >> GOVEE_H5112_PROBE1_SHIFT) & GOVEE_H5112_PROBE1_MASK;
            uint32_t hist_humid_field  = (hist_packed >> GOVEE_H5112_HUMID_SHIFT) & GOVEE_H5112_HUMID_MASK;
            hist_t1[i]  = hist_probe1_field / (double)GOVEE_H5112_FIELD_SCALE - GOVEE_H5112_TEMP_BIAS_C;
            hist_t2[i]  = hist_probe2_field / (double)GOVEE_H5112_FIELD_SCALE - GOVEE_H5112_TEMP_BIAS_C;
            hist_hum[i] = hist_humid_field / (double)GOVEE_H5112_FIELD_SCALE;
        }
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Govee-H5112",
            "id",               "",             DATA_STRING, id_str,
            "id_wire",          "",             DATA_STRING, id_wire_str,
            "battery_ok",       "Battery",      DATA_INT,    battery_pct > 0,
            "battery_pct",      "Battery",      DATA_INT,    battery_pct,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, probe1_c,
            "temperature_2_C",  "Temperature2", DATA_FORMAT, "%.1f C", DATA_DOUBLE, probe2_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%.1f %%", DATA_DOUBLE, humidity,
            NULL);
    /* clang-format on */
    if (has_history) {
        data = data_ary(data, "temperature_C_history",   "Temperature history",  NULL, data_array(GOVEE_H5112_HISTORY_COUNT, DATA_DOUBLE, hist_t1));
        data = data_ary(data, "temperature_2_C_history", "Temperature2 history", NULL, data_array(GOVEE_H5112_HISTORY_COUNT, DATA_DOUBLE, hist_t2));
        data = data_ary(data, "humidity_history",        "Humidity history",     NULL, data_array(GOVEE_H5112_HISTORY_COUNT, DATA_DOUBLE, hist_hum));
    }
    data = data_str(data, "mic", "Integrity", NULL, "CRC");

    decoder_output_data(decoder, data);
    return 1;
}

/* Output fields for -F csv column order and optional field filtering. */
static char const *const output_fields[] = {
        "model",
        "id",
        "id_wire",
        "battery_ok",
        "battery_pct",
        "temperature_C",
        "temperature_2_C",
        "humidity",
        "temperature_C_history",
        "temperature_2_C_history",
        "humidity_history",
        "mic",
        NULL,
};

/* Device registration and demod timing profile. */
r_device const govee_h5112 = {
        .name        = "Govee H5112 Dual-Probe Thermometer",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 2000,
        .decode_fn   = &govee_h5112_decode,
        .fields      = output_fields,
        .priority    = 5, // Between H5310 (priority 0) and H5059 (priority 10).
                          // Claims msg_class 0x13 frames before H5059 does.
};
