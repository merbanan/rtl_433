/** @file
    Watts Vision thermostat (CC110L-based FSK protocol).

    Copyright (C) 2026 Benjamin Larsson <banan@ludd.ltu.se>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn static int watts_vision_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Watts Vision thermostat (CC110L-based FSK protocol).

Not to be confused with the Watts WFHT-RF thermostat (see watts_wfht_rf.c),
an unrelated older OOK/PWM device.

Investigated by \@floe, \@klohner, and \@zuckschwerdt in issue #2885. The
CC110L packet engine produces the preamble, the repeated 0xd391 sync
word, the variable length byte, and the outer CRC-16/CMS (TI's standard
CC110L hardware packet CRC). A separate application controller builds
the payload and appends the inner CRC-16/MODBUS.

Packet layout (all byte-aligned, after the CC110L preamble/sync):

    LEN:8h ID:32h MARKER:8h DEST:32h RECORDS:(LEN-11)*8h CRC_MDB:16h CRC_CMS:16h

- LEN: body length in bytes (everything from ID through CRC_MDB). Always
  0x14 (20, base -> endpoint command/state) or 0x22 (34, endpoint -> base
  status report) in the known captures.
- ID: 32-bit source address (transmitter).
- MARKER: always 0xc6; exact meaning unknown (protocol/port/message-class
  byte). Constant value makes a cheap structural check.
- DEST: 32-bit destination address.
- RECORDS: application data, a sequence of tag+value records (see below).
- CRC_MDB: CRC-16/MODBUS (poly 0x8005 reflected = 0xa001, init 0xffff),
  little-endian, over the data bytes preceding it (ID .. end of records).
- CRC_CMS: CRC-16/CMS (poly 0x8005, init 0xffff, not reflected),
  big-endian, over LEN plus all preceding data bytes (i.e. including
  CRC_MDB). This is the CC110L's own hardware packet CRC.

RECORDS is a sequence of 1-byte-tag + value records, not a fixed layout
tied to message length: the value length is encoded in the tag's own top
two bits, `value_length = (tag >> 6) + 1`, i.e. 1, 2, 3, or 4 bytes for
tag ranges 0x00-0x3f, 0x40-0x7f, 0x80-0xbf, and 0xc0-0xff respectively.
A tag byte of 0x00 terminates the record stream early. Known tags:

- 0x03 (1-byte value): a per-endpoint association/slot id, stable across
  captures from the same endpoint (e.g. 0x04 for one destination, 0x07
  for another). Seen in base -> endpoint messages. Reported as
  association_id.
- 0xdf (4-byte value): four packed state bytes, sent by the base in a
  command/state message. Reported as state_raw (hex); individual bits
  are not decoded.
- 0x3b (1-byte value): a small status/flags byte, always zero in every
  known capture. Reported as flags_raw.
- 0x8d (3-byte value): a compact update of the same state family as
  0xdf, sent by the endpoint in a status report. Its second byte's bit 0
  is a known gate for whether the paired 0x8a value should be treated as
  a setpoint update rather than an informational snapshot (0x11 & 1 = 1
  vs. 0x10 & 1 = 0 across the two known captures); the remaining bits
  are not decoded. Reported as report_flags_0/1/2 (raw bytes).
- 0x8a (3-byte value): the active setpoint, big-endian tenths of a
  degree Fahrenheit (first two bytes), followed by a one-byte operating
  mode (third byte) that selects which per-zone setpoint bank the word
  belongs to: 0x00 Comfort, 0x01 Off, 0x02 Anti-freeze, 0x03
  Reduced/ECO, 0x04 Boost/Timer, 0x08/0x0b scheduled Comfort/Reduced
  phases, 0x10 a manual/temporary override (exact label provisional).
  Reported as mode_setpoint_F and setpoint_mode. This is what resolves
  an earlier apparent discrepancy: 0x8a 0284 03 (64.4 F) does not
  contradict a separately requested 75.2 F target, because it reports
  the Reduced/ECO bank, not a single universal "requested setpoint".
- 0x4b (2-byte value): the primary (air/regulation) measured
  temperature, same big-endian tenths-Fahrenheit scale. Reported as
  temperature_F.
- 0x5e (2-byte value): a secondary floor/external-sensor temperature,
  same scale as 0x4b. Reported as temperature_2_F when present (not
  present in any currently known capture, but the tag, size, and scale
  are established).
- 0xcc (4-byte value): a pair of big-endian floor-limit temperatures,
  same tenths-Fahrenheit scale, meaningful only when the regulation
  sensor (see 0x8e) is FLL. Reported as floor_limit_1_F and
  floor_limit_2_F; which word is the lower vs. upper limit is not yet
  confirmed.
- 0x8e (3-byte value): the installer-configured setpoint range as plain
  integer Celsius (not the tenths-Fahrenheit scale used elsewhere),
  reported as setpoint_min_C/setpoint_max_C, followed by a
  sensor/configuration byte whose low two bits select the regulation
  source (0 Amb, 1 FLR, 2 FLL, 3 Air), reported as sensor_mode; the full
  byte is also reported as sensor_flags_raw since its upper bits are not
  yet decoded.
- 0x4c (2-byte value): a per-zone diagnostic code and a warning-flags
  byte, reported as diagnostic_code and diagnostic_flags; individual
  warning bits are not yet confirmed.

For 0x4b and 0x5e, a raw value of 0x084c is a documented "sensor
unavailable" sentinel (not a real 212.4 F reading); the corresponding
temperature field is omitted rather than reported. For 0xcc, a raw word
of 0 means that floor limit is inactive/unavailable, and is likewise
omitted rather than reported as 0.0 F.

Unrecognized tags are logged and skipped rather than guessed at from
position alone, since the record stream is walked generically instead
of assuming a fixed per-message-type sequence.

Both CRCs are verified across all known captures before any measurement
is emitted. All fields above should still be treated as experimental:
the record grammar and the 0x4b/0x8a/0x8e scale and mode/sensor mappings
are confirmed against every known capture, but only two independent
endpoints (both in Air regulation mode, both with unset floor limits and
no diagnostic flags set) have been checked so far.
*/

#define WATTS_VISION_MARKER 0xc6
#define WATTS_VISION_TYPE_SHORT 0x14
#define WATTS_VISION_TYPE_LONG  0x22

#define WATTS_VISION_TEMP_UNAVAILABLE 0x084c

// Mode enum following tag 0x8a's setpoint word (see file doc comment).
static char const *watts_vision_setpoint_mode_str(uint8_t mode)
{
    switch (mode) {
    case 0x00: return "Comfort";
    case 0x01: return "Off";
    case 0x02: return "Anti-freeze";
    case 0x03: return "Reduced/ECO";
    case 0x04: return "Boost/Timer";
    case 0x08: return "Auto (Comfort phase)";
    case 0x0b: return "Auto (Reduced phase)";
    case 0x10: return "Manual/Temporary";
    default:   return "unknown";
    }
}

// Regulation-source selector: low two bits of tag 0x8e's third byte.
static char const *watts_vision_sensor_mode_str(uint8_t sensor_flags)
{
    switch (sensor_flags & 0x3) {
    case 0: return "Amb";
    case 1: return "FLR";
    case 2: return "FLL";
    default: return "Air"; // case 3
    }
}

static int watts_vision_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // 40-bit anchor: aa + repeated 0xd391 sync. Finds the frame despite
    // the occasional bit error at the very start.
    uint8_t const preamble_pattern[] = {0xaa, 0xd3, 0x91, 0xd3, 0x91};

    // FSK_PULSE_PCM produces exactly one row per transmission (unlike OOK
    // decoders that accumulate repeats as separate rows), so there is
    // never more than one candidate frame to check here.
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int row          = 0;
    unsigned row_len = bitbuffer->bits_per_row[row];

    unsigned bitpos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, sizeof(preamble_pattern) * 8);
    if (bitpos >= row_len) {
        return DECODE_ABORT_EARLY;
    }
    bitpos += sizeof(preamble_pattern) * 8;

    // Need at least the length byte (1) to know the body size.
    if (bitpos + 8 > row_len) {
        decoder_log(decoder, 2, __func__, "Truncated before length byte");
        return DECODE_ABORT_LENGTH;
    }

    // Peek the length byte to size the frame: total on-air bytes = LEN + 3.
    uint8_t lenb[1];
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, lenb, 8);
    int length = lenb[0];
    if (length != WATTS_VISION_TYPE_SHORT && length != WATTS_VISION_TYPE_LONG) {
        decoder_logf(decoder, 2, __func__, "Unsupported length byte: 0x%02x", length);
        return DECODE_ABORT_EARLY;
    }

    int total_bits = (length + 3) * 8;
    if (bitpos + total_bits > row_len) {
        decoder_logf(decoder, 2, __func__, "Message too short (%d bits, need %d)", row_len - bitpos, total_bits);
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[40] = {0}; // long message is 37 bytes; 40 leaves margin
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, total_bits);

    // CRC_MDB: CRC-16/MODBUS over the LEN-2 data bytes (b[1 .. LEN-2]),
    // little-endian, stored at b[LEN-1], b[LEN].
    uint16_t crc_mdb_calc = crc16lsb(&b[1], length - 2, 0xa001, 0xffff);
    uint16_t crc_mdb      = (b[length] << 8) | b[length - 1];
    if (crc_mdb_calc != crc_mdb) {
        decoder_log(decoder, 2, __func__, "CRC_MDB fail");
        return DECODE_FAIL_MIC;
    }

    // CRC_CMS: CRC-16/CMS over LEN + data (b[0 .. LEN]), big-endian,
    // stored at b[LEN+1] (high byte), b[LEN+2] (low byte).
    uint16_t crc_cms_calc = crc16(b, length + 1, 0x8005, 0xffff);
    uint16_t crc_cms      = (b[length + 1] << 8) | b[length + 2];
    if (crc_cms_calc != crc_cms) {
        decoder_log(decoder, 2, __func__, "CRC_CMS fail");
        return DECODE_FAIL_MIC;
    }

    if (b[5] != WATTS_VISION_MARKER) {
        decoder_logf(decoder, 2, __func__, "unexpected marker byte: %02x", b[5]);
        return DECODE_FAIL_SANITY;
    }

    uint32_t id   = ((uint32_t)b[1] << 24) | (b[2] << 16) | (b[3] << 8) | b[4];
    uint32_t dest = ((uint32_t)b[6] << 24) | (b[7] << 16) | (b[8] << 8) | b[9];
    // Formatted as hex strings, not DATA_FORMAT+DATA_INT: these are
    // 32-bit unsigned addresses, and JSON output ignores DATA_FORMAT
    // and prints DATA_INT as a signed decimal, which would show
    // addresses with the high bit set (e.g. 0xd0374654) as negative.
    char id_str[9];
    char dest_str[9];
    snprintf(id_str, sizeof(id_str), "%08x", id);
    snprintf(dest_str, sizeof(dest_str), "%08x", dest);

    char const *msg_type = (length == WATTS_VISION_TYPE_SHORT) ? "command" : "status";

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",            DATA_STRING, "Watts-Vision",
            "id",       "",            DATA_STRING, id_str,
            "dest",     "",            DATA_STRING, dest_str,
            "msg_type", "",            DATA_STRING, msg_type,
            NULL);
    /* clang-format on */

    // Walk the record stream generically: value length is encoded in
    // the tag's top two bits, so the same walker covers both short
    // and long messages and any future tag not yet seen. See file
    // doc comment for the grammar and the known-tag table.
    int records_len = length - 11;
    int pos          = 0;
    while (pos < records_len) {
        uint8_t tag = b[10 + pos];
        if (tag == 0x00) {
            break; // parser terminator, not an ordinary tag
        }
        int value_len = (tag >> 6) + 1;
        if (pos + 1 + value_len > records_len) {
            decoder_logf(decoder, 2, __func__, "record tag %02x overruns records", tag);
            break;
        }
        uint8_t const *val = &b[10 + pos + 1];

        switch (tag) {
        case 0x03: // association/slot id (short message)
            data = data_int(data, "association_id", "", NULL, val[0]);
            break;
        case 0xdf: { // packed state S0..S3 (short message)
            char state_str[9];
            snprintf(state_str, sizeof(state_str), "%02x%02x%02x%02x", val[0], val[1], val[2], val[3]);
            data = data_str(data, "state_raw", "", NULL, state_str);
            break;
        }
        case 0x3b: // status/flags byte (short message)
            data = data_int(data, "flags_raw", "", "%02x", val[0]);
            break;
        case 0x8d: // report/update state (long message)
            data = data_int(data, "report_flags_0", "", "%02x", val[0]);
            data = data_int(data, "report_flags_1", "", "%02x", val[1]);
            data = data_int(data, "report_flags_2", "", "%02x", val[2]);
            break;
        case 0x8a: { // active setpoint + mode (long message)
            int setpoint_raw = (val[0] << 8) | val[1];
            if (setpoint_raw != WATTS_VISION_TEMP_UNAVAILABLE) {
                data = data_dbl(data, "mode_setpoint_F", "", "%.1f", setpoint_raw / 10.0f);
            }
            data = data_str(data, "setpoint_mode", "", NULL, watts_vision_setpoint_mode_str(val[2]));
            break;
        }
        case 0x4b: { // primary measured temperature (long message)
            int temperature_raw = (val[0] << 8) | val[1];
            if (temperature_raw != WATTS_VISION_TEMP_UNAVAILABLE) {
                data = data_dbl(data, "temperature_F", "", "%.1f", temperature_raw / 10.0f);
            }
            break;
        }
        case 0x5e: { // secondary floor/external temperature (long message)
            int temperature_2_raw = (val[0] << 8) | val[1];
            if (temperature_2_raw != WATTS_VISION_TEMP_UNAVAILABLE) {
                data = data_dbl(data, "temperature_2_F", "", "%.1f", temperature_2_raw / 10.0f);
            }
            break;
        }
        case 0xcc: { // floor-limit pair (long message)
            int floor_limit_1_raw = (val[0] << 8) | val[1];
            int floor_limit_2_raw = (val[2] << 8) | val[3];
            if (floor_limit_1_raw != 0) {
                data = data_dbl(data, "floor_limit_1_F", "", "%.1f", floor_limit_1_raw / 10.0f);
            }
            if (floor_limit_2_raw != 0) {
                data = data_dbl(data, "floor_limit_2_F", "", "%.1f", floor_limit_2_raw / 10.0f);
            }
            break;
        }
        case 0x8e: // setpoint bounds + sensor mode (long message)
            data = data_int(data, "setpoint_min_C", "", NULL, val[0]);
            data = data_int(data, "setpoint_max_C", "", NULL, val[1]);
            data = data_str(data, "sensor_mode", "", NULL, watts_vision_sensor_mode_str(val[2]));
            data = data_int(data, "sensor_flags_raw", "", "%02x", val[2]);
            break;
        case 0x4c: // diagnostic code + flags (long message)
            data = data_int(data, "diagnostic_code", "", "%02x", val[0]);
            data = data_int(data, "diagnostic_flags", "", "%02x", val[1]);
            break;
        default:
            decoder_logf(decoder, 2, __func__, "unknown record tag %02x (%d-byte value)", tag, value_len);
            break;
        }

        pos += 1 + value_len;
    }

    data = data_str(data, "mic", "", NULL, "CRC");

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "dest",
        "msg_type",
        "association_id",
        "state_raw",
        "flags_raw",
        "temperature_F",
        "temperature_2_F",
        "mode_setpoint_F",
        "setpoint_mode",
        "setpoint_min_C",
        "setpoint_max_C",
        "sensor_mode",
        "sensor_flags_raw",
        "floor_limit_1_F",
        "floor_limit_2_F",
        "diagnostic_code",
        "diagnostic_flags",
        "report_flags_0",
        "report_flags_1",
        "report_flags_2",
        "mic",
        NULL,
};

r_device const watts_vision = {
        .name        = "Watts Vision thermostat (-f 868.3M)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 26,
        .long_width  = 26,
        .reset_limit = 1000,
        .decode_fn   = &watts_vision_decode,
        .fields      = output_fields,
};
