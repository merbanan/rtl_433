/** @file
    Decoder for Olympia Protect 9571 alarm system sensors.

    Copyright (C) 2026 Paweł Łabuda

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Olympia Protect 9571 alarm system sensor protocol.

The devices transmit around 868.3–868.5 MHz using FSK-PCM modulation
with a fixed 366 us bit width (short_width = long_width = 366 us),
preamble aa aa 2d d4 followed by a 48-bit payload.

Packet bit layout (MSB-first, 6 bytes = 48 bits):

    NNNN NNNN  NNNN NNNN  NNNN NNNN  XXXX IYYY  WZZZ TTTS  CCCC CCCC
    byte 0     byte 1     byte 2     byte 3     byte 4     byte 5

- N[23:0]  device ID          @0:{24}
- X[3:0]   constant = 0x8     @24:{4}    (marker, always 0x8)
- I        battery inserted   @28:{1}
- Y[2:0]   constant = 0x6     @29:{3}    (marker, always 0x6)
- W        battery weak       @32:{1}
- Z[2:0]   device class       @33:{3}    0x0=sensor  0x1=keyfob
- T[2:0]   type / command     @36:{3}    see tables below
- S        state / button     @39:{1}    Contact: 0=closed, 1=open
                                         PIR-Motion: 0=idle, 1=motion
                                         Keyfob: 1 for most buttons, 0 for SOS
- C[7:0]   checksum           @40:{8}    sum(bytes 0-4) & 0xFF

sensor_type values  (device_class == 0x0):
- 0x4 (100b) = Contact
- 0x7 (111b) = PIR-Motion

keyfob command values  (device_class == 0x1):
- 0x3 (011b) = disarm
- 0x4 (100b) = arm home + siren
- 0x5 (101b) = arm siren only
- 0x6 (110b) = arm home only
- 0x7 (111b) = SOS / panic
*/

static int olympia_9571_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xAA, 0x2D, 0xD4};
    uint8_t b[6];
    unsigned bit_offset;

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_LENGTH;
    }

    bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8);
    if (bit_offset >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }
    bit_offset += sizeof(preamble) * 8;

    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    int id           = (b[0] << 16) | (b[1] << 8) | b[2];
    int marker_a     = (b[3] >> 4) & 0x0f;
    int marker_b     = b[3] & 0x07;
    int batt_weak    = (b[4] >> 7) & 0x01;
    int device_class = (b[4] >> 4) & 0x07;
    int sensor_type  = (b[4] >> 1) & 0x07;
    int sensor_state = b[4] & 0x01;

    if (marker_a != 0x8 || marker_b != 0x6) {
        decoder_log(decoder, 1, __func__, "marker mismatch");
        return DECODE_FAIL_SANITY;
    }

    if ((add_bytes(b, 5) & 0xff) != b[5]) {
        decoder_log_bitrow(decoder, 1, __func__, b, sizeof(b) * 8, "Checksum error");
        return DECODE_FAIL_MIC;
    }

    char const *model   = "Olympia-9571";
    char const *subtype = NULL;
    char const *state   = NULL;
    char const *cmd     = NULL;
    int has_state       = 0;
    int has_cmd         = 0;
    int cmd_id          = 0;

    /* clang-format off */
    if (device_class == 0x0) {
        if (sensor_type == 0x4) {
            subtype   = "Contact";
            state     = sensor_state ? "open" : "closed";
            has_state = 1;
        } else if (sensor_type == 0x7) {
            subtype   = "PIR-Motion";
            state     = sensor_state ? "motion" : "idle";
            has_state = 1;
        }
    } else if (device_class == 0x1) {
        subtype = "Keyfob";
        has_cmd = 1;
        cmd_id  = sensor_type;
        switch (cmd_id) {
        case 0x3: cmd = "Disarm";          break;
        case 0x4: cmd = "Arm Home + Siren"; break;
        case 0x5: cmd = "Arm Siren Only";   break;
        case 0x6: cmd = "Arm Home Only";    break;
        case 0x7: cmd = "SOS";              break;
        default:  cmd = "Unknown";          break;
        }
    }

    if (!subtype) {
        decoder_logf(decoder, 1, __func__, "unknown device class/type");
        return DECODE_FAIL_SANITY;
    }
    /* clang-format on */

    /* clang-format off */
    data_t *data = data_make(
            "model",      "Model",     DATA_STRING, model,
            "subtype",    "Device",    DATA_STRING, subtype,
            "id",         "ID",        DATA_FORMAT, "%06x", DATA_INT, id,
            "state",      "State",     DATA_COND, has_state, DATA_STRING, state ? state : "",
            "cmd",        "CMD",       DATA_COND, has_cmd, DATA_STRING, cmd ? cmd : "",
            "cmd_id",     "CMD_ID",    DATA_COND, has_cmd, DATA_INT, cmd_id,
            "battery_ok", "Battery",   DATA_INT,   !batt_weak,
            "mic",        "Integrity", DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "subtype",
        "id",
        "state",
        "cmd",
        "cmd_id",
        "battery_ok",
        "mic",
        NULL,
};

r_device const olympia_9571 = {
        .name        = "Olympia Protect 9571 alarm system sensors",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 366,
        .long_width  = 366,
        .reset_limit = 5000,
        .decode_fn   = &olympia_9571_decode,
        .fields      = output_fields,
};
