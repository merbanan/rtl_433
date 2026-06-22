/** @file
    Govee Dual-Probe Thermometer H5112.

    Copyright (C) 2026 Paul Antaki (\@Pablo1)

    Based on the Govee H5059 decoder by Reece Neff <reeceneff@gmail.com>
    (rtl_433 PR #3493), which reverse-engineered the shared Govee FSK
    protocol used by this device family -- the 2c 4c 4a sync words, the
    128-byte XOR key, and the CRC-16/AUG-CCITT framing. That protocol
    foundation is reused here unchanged.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"
#include <math.h>

#define GOVEE_H5112_SYNC_LEN  3
#define GOVEE_H5112_MIN_FRAME 7
#define GOVEE_H5112_MAX_FRAME 128
#define GOVEE_H5112_KEY_LEN   128

#define GOVEE_H5112_CRC_POLY  0x1021
#define GOVEE_H5112_CRC_INIT  0x1d0f

// msg_class values this decoder handles.
#define GOVEE_H5112_MSG_PERIODIC  0x13  // autonomous periodic report (~10 min)
#define GOVEE_H5112_MSG_TRIGGERED 0x71  // gateway-triggered status response

// Minimum decrypted bytes needed to read all sensor fields (through dec[9]).
#define GOVEE_H5112_MIN_DEC 10

// Byte offsets in the decrypted payload.
#define GOVEE_H5112_MSG_CLASS_OFFSET 0
#define GOVEE_H5112_ID_OFFSET        1  // 4 bytes (big-endian wire order)
#define GOVEE_H5112_BATTERY_OFFSET   5  // battery percent (0x64 = 100%)
#define GOVEE_H5112_PROBE2_OFFSET    6  // probe 2 temperature (8-bit modular counter)
#define GOVEE_H5112_PROBE1_FINE      7  // probe 1 temperature, low 8 bits of 14-bit ADC value
#define GOVEE_H5112_PROBE1_COARSE    8  // probe 1 temperature, high 6 bits [5:0]; bits [7:6] = purpose unknown
#define GOVEE_H5112_HUMID_OFFSET     9  // probe 1 humidity

// Probe 1 temperature: 14-bit fixed-point ADC value at 80 counts per °C.
// Combined value = dec[PROBE1_FINE] + (dec[PROBE1_COARSE] & 0x3F) * 256.
// T_C = (combined - GOVEE_H5112_PROBE1_BIAS) / GOVEE_H5112_PROBE1_SCALE
// Bias = 3200 places 0 °C at the midpoint of the 14-bit range.
#define GOVEE_H5112_PROBE1_BIAS  3200
#define GOVEE_H5112_PROBE1_SCALE 80.0f

// Probe 2 temperature: 8-bit modular counter at 10 counts per °C, bias 144.
// T_C = (dec[PROBE2_OFFSET] - GOVEE_H5112_PROBE2_BIAS) / GOVEE_H5112_PROBE2_SCALE
// Wraps every 25.6 °C; absolute value requires stateful wrap-count tracking.
#define GOVEE_H5112_PROBE2_BIAS  144
#define GOVEE_H5112_PROBE2_SCALE 10.0f
#define GOVEE_H5112_PROBE2_WRAP  25.6f  // modular period in °C
#define GOVEE_H5112_MAX_DEVICES  8      // max simultaneously tracked devices

// Humidity: dec[HUMID_OFFSET] * GOVEE_H5112_HUMID_SCALE = % RH.
#define GOVEE_H5112_HUMID_SCALE 0.4f

// History buffer: dec[17-56], 10 × 4-byte records of [d6, d7, d8, d9],
// sampled at ~1-minute intervals within the 10-minute reporting window,
// oldest record first (dec[17-20] oldest, dec[53-56] most recent).
#define GOVEE_H5112_HISTORY_OFFSET 17
#define GOVEE_H5112_HISTORY_COUNT  10


// Per-device state for probe-2 wrap-count tracking.
// k is initialised to 0 on first decode; updated by detecting 25.6 °C boundary
// crossings between consecutive frames (|Δt2_mod| > 12.8 °C).
// T_true = T_mod + k * GOVEE_H5112_PROBE2_WRAP
static struct {
    uint32_t id_wire;
    float    t2_mod; // last raw modular value before k-correction
    int      k;
    int      valid;
} govee_h5112_devs[GOVEE_H5112_MAX_DEVICES];

static uint8_t const govee_h5112_sync[]       = {0x2c, 0x4c, 0x4a};
static uint8_t const govee_h5112_sync_skew1[] = {0x16, 0x26, 0x25};
static uint8_t const govee_h5112_key[GOVEE_H5112_KEY_LEN + 1] =
        "s6amyEvO8UslCY0eZjgc2S6APCVLgLxzFvL2Z5GWPW7fKVjy2oAU6uiKU3lZCHm62VYQQuCtgxzPgGd8UDRPVZpDRAsh5EdYq1E4j4morJ3vd6tWx8BiWOLDc2I8wKUK";

/**
Govee Dual-Probe Thermometer H5112.

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

    byte:   0    1  2  3  4   5     6       7   8     9    10  11   12  13    14-16   17-56
    field:  13  <id_wire 4B>  bat  p2_temp  p1_fine  p1_coarse  hum  00  <time_s LE>  const  <history>

Triggered status response (msg_class 0x71), 28-byte decrypted payload.
Sent in response to a gateway poll (msg_class 0x70 ping). Contains the
same sensor fields as 0x13 in the same byte positions, but omits the
history buffer, seconds counter, and constant marker bytes. Humidity is
present in dec[9]; the app may display the cached 0x13 value instead when
humidity appears unchanged between triggers.

    byte:   0    1  2  3  4   5     6       7   8     9    10   11  12  13
    field:  71  <id_wire 4B>  bat  p2_temp  p1_fine  p1_coarse  hum  ?   00  <session_id LE>

- dec[0]:  0x13 or 0x71, msg_class (identifies this frame type)
- dec[1-4]: device ID in wire byte order (big-endian); lower 16 bits are a
    pairing token shared by all devices bound to the same gateway
- dec[5]:  battery percent (0x64 = 100%)
- dec[6]:  probe 2 temperature, 8-bit modular counter;
    T_C = (dec[6] - 144) / 10.0  (mod 25.6 °C; wraps every 25.6 °C)
    Absolute temperature requires stateful wrap-count tracking across frames;
    see the k-tracking logic in govee_h5112_decode().
- dec[7]:  probe 1 temperature, low 8 bits of 14-bit ADC value (fine)
- dec[8]:  bits [5:0] = probe 1 temperature, high 6 bits of 14-bit ADC value (coarse)
    T_C = (dec[7] + (dec[8] & 0x3F) * 256 - 3200) / 80.0
    Absolute temperature, no wrapping. Range: -40 °C to > 60 °C.
    Resolution: 0.0125 °C (reported to 0.1 °C precision).
    bits [7:6] = purpose unknown; all four values appear across frames and
    within the history buffer with no discernible pattern. Originally
    hypothesised to be a rolling packet counter, but confirmed NOT cyclic
    (0→1→2→3→0) and NOT a probe 2 temperature-range indicator.
- dec[9]:  probe 1 humidity; humidity_pct = dec[9] * 0.4
- dec[10]: status/flags byte. bit[7] = display unit (0=°C, 1=°F), confirmed from
    gateway unit-change command responses. bit[6] = config/state flag of
    undetermined meaning; usually differs between units (one unit sets it, another
    clears it) but it is NOT a fixed per-device constant -- one unit was observed
    with it both set and clear. Verified NOT correlated with probe temperature or
    the probe 2 wrap count k (checked over 86 frames spanning probe 1 +0.4..+25.9 °C
    and probe 2 modular -11.1 °C). bits[5:0] = 0 in all captures.
    NOT a probe-swap flag (the two probe connectors are physically different).
- dec[11]: 0x00 (reserved or padding; full byte unused across all captures)
- dec[12-13]: seconds counter, little-endian 16-bit, increments at 1 Hz
- dec[14-15]: 0x37 0x6a (constant marker bytes, observed stable across all captures;
    earlier firmware builds showed 0x36 0x6a)
- dec[16]: history record count; normally 10 (0x0a), less on a freshly powered device
- dec[17-56]: rolling history buffer -- 10 x 4-byte records (oldest first),
    each record mirrors [dec[6], dec[7], dec[8], dec[9]] from that period

Probe 1 temperature derivation:

dec[7] and dec[8][5:0] together form a 14-bit fixed-point ADC value at
80 counts per °C with a bias of 3200 (= 40 °C * 80), placing 0 °C at the
midpoint of the counter range. The encoding is equivalent to a 14-bit
integer split across two bytes; the high byte's top 2 bits (dec[8][7:6])
have an unknown purpose and are masked out.

Formula: T_C = (dec[7] + (dec[8] & 0x3F) * 256 - 3200) / 80.0

Verified against confirmed app readings:
- dec[7]=0x98, dec[8][5:0]=14: (152 + 3584 - 3200) / 80 =  6.700 °C (app: 6.7 °C, error 0.000)
- dec[7]=0x9a, dec[8][5:0]=20: (154 + 5120 - 3200) / 80 = 25.925 °C (app: 25.9 °C, error 0.025)
- dec[7]=0xa2, dec[8][5:0]=20: (162 + 5120 - 3200) / 80 = 26.025 °C (app: 26.0 °C, error 0.025)
- dec[7]=0xa9, dec[8][5:0]=5:  (169 + 1280 - 3200) / 80 = -21.888 °C (freezer, no app reading)

Probe 2 temperature derivation:

dec[6] is an 8-bit modular counter at 10 counts per °C with bias 144:
  dec[6] = (T_probe2 * 10 + 144) mod 256

The modular value T_mod = (dec[6] - 144) / 10.0 is exact but wraps every
25.6 °C. Recovering absolute temperature requires tracking the wrap count k
across consecutive frames and applying T = T_mod + k * 25.6.

This decoder maintains per-device k state and seeds it on first decode using
T1 ≈ T2 (correct when both probes start at the same temperature, which is the
normal case -- the Govee app requires pairing at room temperature before
deploying the probes). If rtl_433 is restarted while probe 2 is already in a
cold environment, the seeded k may be wrong by ±1 until the next 25.6 °C
boundary crossing self-corrects it.

Humidity derivation:

dec[9] * 0.4 = % RH (probe 1 sensor only; probe 2 is temperature-only).
Verified exact against app readings at room temperature:
- dec[9]=0x75 (117): 117 * 0.4 = 46.8 % (app: 46.8 %, error 0.0)
- dec[9]=0xf2 (242): 242 * 0.4 = 96.8 % (app: 97.1 %, error 0.3)

Device ID:

`id_wire` is the full 32-bit on-wire device ID (e.g. 0x0b279cb2); `id`
is the same value with its two 16-bit half-words swapped (0x9cb20b27),
following the convention of the Govee H5059 and H5310 decoders. The low
16 bits of id_wire are a pairing token shared by all devices bound to
the same gateway (0x9cb2 in all observed captures).

Priority and coexistence:

This decoder runs at priority 5, after the H5310 decoder (priority 0,
which claims LL=0x10/0x1c/0x1f/0x3d frames first) but before the H5059
decoder (priority 10). H5112 frames carry a variable outer length; they
are claimed by checking dec[0] == 0x13 or 0x71 post-decryption.
The H5059 decoder would otherwise report these as event "Unknown".

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
    Both probes are at room temperature at pairing time by design, so the
    initial wrap count k=0 is always correct when the device is first set up.
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
        return DECODE_ABORT_EARLY;
    }

    uint32_t id_wire = ((uint32_t)dec[GOVEE_H5112_ID_OFFSET] << 24)
            | ((uint32_t)dec[GOVEE_H5112_ID_OFFSET + 1] << 16)
            | ((uint32_t)dec[GOVEE_H5112_ID_OFFSET + 2] << 8)
            | dec[GOVEE_H5112_ID_OFFSET + 3];
    // Canonical ID: two 16-bit half-words swapped, matching govee_h5059/h5310.
    uint32_t id = ((id_wire & 0xffff) << 16) | ((id_wire >> 16) & 0xffff);

    int battery_pct = dec[GOVEE_H5112_BATTERY_OFFSET];

    // Probe 1 temperature: 14-bit ADC value at 80 counts/°C, bias 3200 (= 40 °C * 80).
    int probe1_raw = (int)dec[GOVEE_H5112_PROBE1_FINE]
            + (int)(dec[GOVEE_H5112_PROBE1_COARSE] & 0x3F) * 256
            - GOVEE_H5112_PROBE1_BIAS;
    float probe1_c = probe1_raw / GOVEE_H5112_PROBE1_SCALE;

    // Probe 2 temperature: 8-bit modular counter at 10 counts/°C, bias 144.
    // Wraps every 25.6 °C.  Recover absolute temperature by tracking wrap-count k
    // per device across consecutive frames.  k starts at 0 on first decode; a jump
    // of |Δt2_mod| > 12.8 °C between frames indicates a boundary crossing.
    char  probe2_raw_str[5]; // "0xNN\0"
    snprintf(probe2_raw_str, sizeof(probe2_raw_str), "0x%02x", dec[GOVEE_H5112_PROBE2_OFFSET]);
    float probe2_mod = ((int)dec[GOVEE_H5112_PROBE2_OFFSET] - GOVEE_H5112_PROBE2_BIAS)
            / GOVEE_H5112_PROBE2_SCALE;
    float probe2_c = probe2_mod; // updated below with k-correction
    int   probe2_k = 0;          // wrap count, also applied to history records
    for (int i = 0; i < GOVEE_H5112_MAX_DEVICES; i++) {
        if (govee_h5112_devs[i].valid && govee_h5112_devs[i].id_wire == id_wire) {
            float delta = probe2_mod - govee_h5112_devs[i].t2_mod;
            if (delta >= GOVEE_H5112_PROBE2_WRAP * 0.5f)
                govee_h5112_devs[i].k--;
            else if (delta <= -GOVEE_H5112_PROBE2_WRAP * 0.5f)
                govee_h5112_devs[i].k++;
            govee_h5112_devs[i].t2_mod = probe2_mod;
            probe2_k = govee_h5112_devs[i].k;
            probe2_c = probe2_mod + probe2_k * GOVEE_H5112_PROBE2_WRAP;
            break;
        }
        if (!govee_h5112_devs[i].valid) {
            // Seed k from probe 1 (assumes T2 ≈ T1 on first decode; wrong if
            // probes are in different environments, but best available guess).
            int k0 = (int)roundf((probe1_c - probe2_mod) / GOVEE_H5112_PROBE2_WRAP);
            govee_h5112_devs[i].id_wire = id_wire;
            govee_h5112_devs[i].t2_mod  = probe2_mod;
            govee_h5112_devs[i].k       = k0;
            govee_h5112_devs[i].valid   = 1;
            probe2_k = k0;
            probe2_c = probe2_mod + k0 * GOVEE_H5112_PROBE2_WRAP;
            break;
        }
    }

    // Probe 1 humidity: dec[9] * 0.4 = % RH.
    float humidity = dec[GOVEE_H5112_HUMID_OFFSET] * GOVEE_H5112_HUMID_SCALE;

    // History buffer: 10 × 4-byte records of [d6, d7, d8, d9], oldest first.
    // Same k correction applied to T2 as the current frame (all records are
    // from the same 10-minute window so k is stable, except during a wrap
    // crossing, where the oldest records may be off by one period).
    double hist_t1[GOVEE_H5112_HISTORY_COUNT];
    double hist_t2[GOVEE_H5112_HISTORY_COUNT];
    double hist_hum[GOVEE_H5112_HISTORY_COUNT];
    int has_history = (msg_class == GOVEE_H5112_MSG_PERIODIC)
            && (enc_len >= GOVEE_H5112_HISTORY_OFFSET + GOVEE_H5112_HISTORY_COUNT * 4);
    if (has_history) {
        for (int i = 0; i < GOVEE_H5112_HISTORY_COUNT; i++) {
            unsigned base = GOVEE_H5112_HISTORY_OFFSET + (unsigned)i * 4;
            int ht1_raw = (int)dec[base + 1]
                    + (int)(dec[base + 2] & 0x3F) * 256
                    - GOVEE_H5112_PROBE1_BIAS;
            hist_t1[i]  = ht1_raw / (double)GOVEE_H5112_PROBE1_SCALE;
            hist_t2[i]  = ((int)dec[base] - GOVEE_H5112_PROBE2_BIAS) / (double)GOVEE_H5112_PROBE2_SCALE
                    + probe2_k * GOVEE_H5112_PROBE2_WRAP;
            hist_hum[i] = dec[base + 3] * (double)GOVEE_H5112_HUMID_SCALE;
        }
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Govee-H5112",
            "id",               "",             DATA_FORMAT, "%08x", DATA_INT, id,
            "id_wire",          "",             DATA_FORMAT, "%08x", DATA_INT, id_wire,
            "battery_ok",       "Battery",      DATA_INT,    battery_pct > 0,
            "battery_pct",      "Battery",      DATA_INT,    battery_pct,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.3f C", DATA_DOUBLE, (double)probe1_c,
            "temperature_2_C",  "Temperature2", DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)probe2_c,
            "temperature_2_raw","Temperature2 raw byte", DATA_STRING, probe2_raw_str,
            "humidity",         "Humidity",     DATA_FORMAT, "%.1f %%", DATA_DOUBLE, (double)humidity,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */
    if (has_history) {
        data = data_ary(data, "temperature_C_history",   "Temperature history",  NULL, data_array(GOVEE_H5112_HISTORY_COUNT, DATA_DOUBLE, hist_t1));
        data = data_ary(data, "temperature_2_C_history", "Temperature2 history", NULL, data_array(GOVEE_H5112_HISTORY_COUNT, DATA_DOUBLE, hist_t2));
        data = data_ary(data, "humidity_history",        "Humidity history",     NULL, data_array(GOVEE_H5112_HISTORY_COUNT, DATA_DOUBLE, hist_hum));
    }

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
        "temperature_2_raw",
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
