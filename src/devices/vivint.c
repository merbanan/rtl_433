/** @file
    Vivint Door/Window Sensors (345 MHz).

    Copyright (C) 2026 Benjamin Larsson <banan@ludd.ltu.se>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define VIVINT_MSG_BIT_LEN 80

/**
Vivint Door/Window Sensors (345.0 MHz).

Tested with the Vivint V-DW21R-345 door/window sensor.

The framing is closely related to the Honeywell/2GIG (Ademco) 345 MHz
protocol (see honeywell.c): OOK Manchester (zerobit) with a 0xFFFE
preamble and a 96 bit (12 byte) packet.

Decoded payload (80 data bits, 10 bytes) after the 0xFFFE preamble:

    TT CC CC FF II II II II RR RR

- T: 8 bit frame subtype, confirmed by a firmware disassembly (MSP430 flash
     dump) to identify the Vivint sensor family sending the frame, not an
     event kind: 0x7a = DW11 door/window, 0x79 = GB2 glass-break,
     0x74 = PIR2 motion, 0x72/0x73/0x76 = other sensor families (mentioned
     in the thread but not confirmed against a captured sample here),
     0xd0 = power-on/startup beacon sent by any sensor family
- C: 16 bit counter, increments with every transmission (confirmed against
     a ~14000 message capture with a steadily incrementing count)
- F: 8 bit unexplained field; the low 2 bits are always zero, the other 6
     bits vary but no correlation with door state was found
- I: 32 bit device identifier
- R: 16 bit CRC

I decodes to the sensor's printed TXID: split the 32 bits into a 12 bit
and a 20 bit decimal number, e.g. 0x0137beda -> 19, 507610 -> printed as
"0019-0507610" (matches label "0019-050-7610"). Confirmed against 3
independent sensors. Exposed as the `id` field. Same scheme as used for
Honeywell/2GIG in issue #1261.

All non-0xd0 subtypes use a packed 12-bit CRC scheme, confirmed against
the disassembled firmware:
  - CRC-16 poly 0x8050 over b[0..7] + top_nibble(b[8])  (9 bytes)
  - check12 = crc16 >> 4; stored12 = (low_nibble(b[8]) << 8) | b[9]
  - valid when check12 == stored12

The 0xd0 frames use standard CRC-16 poly 0x8050 over b[0..7].

See https://github.com/merbanan/rtl_433/issues/1504
*/

static int vivint_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[2] = {0xff, 0xe0}; // 12 bits of 0xFFFE

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }
    int row = 0;

    decoder_log_bitrow(decoder, 2, __func__, bitbuffer->bb[row], bitbuffer->bits_per_row[row], "MSG");

    bitbuffer_invert(bitbuffer);

    int pos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, 12) + 12;
    int len = bitbuffer->bits_per_row[row] - pos;
    if (len < VIVINT_MSG_BIT_LEN) {
        decoder_logf(decoder, 2, __func__, "Too short (%d bits after preamble)", len);
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[VIVINT_MSG_BIT_LEN / 8 + 1];
    bitbuffer_extract_bytes(bitbuffer, row, pos, b, VIVINT_MSG_BIT_LEN);
    decoder_log_bitrow(decoder, 2, __func__, b, VIVINT_MSG_BIT_LEN, "MSG (inverted, aligned)");

    int event_type  = b[0];
    int counter     = (b[1] << 8) | b[2];
    int flags       = b[3]; // low 2 bits always zero, other 6 bits unexplained
    unsigned id     = ((unsigned)b[4] << 24) | ((unsigned)b[5] << 16) | ((unsigned)b[6] << 8) | b[7];
    int crc         = (b[8] << 8) | b[9];

    if (id == 0 || id == 0xffffffff) {
        decoder_logf(decoder, 2, __func__, "Id sanity check failed (%08x)", id);
        return DECODE_FAIL_SANITY;
    }

    /* CRC check */
    int crc_valid = 0;
    if (event_type == 0xd0) {
        if (crc == crc16(b, 8, 0x8050, 0)) crc_valid = 1;
    }
    else {
        uint8_t b8_full = b[8];
        b[8] &= 0xF0;
        int crc_full = crc16(b, 9, 0x8050, 0);
        b[8]         = b8_full;
        int check12  = crc_full >> 4;
        int stored12 = ((b8_full & 0x0F) << 8) | b[9];
        if (check12 == stored12) crc_valid = 1;
    }

    if (!crc_valid) {
        decoder_logf(decoder, 2, __func__, "CRC check failed");
        return DECODE_FAIL_MIC;
    }

    // the sensor's printed TXID is the 32 bit id split into a 12 bit and a 20 bit decimal number
    char id_str[13];
    snprintf(id_str, sizeof(id_str), "%04u-%07u", (id >> 20) & 0xfff, id & 0xfffff);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",              DATA_STRING, "Vivint-Security",
            "id",           "",              DATA_STRING, id_str,
            "counter",      "",              DATA_FORMAT, "%04x", DATA_INT, counter,
            "flags",        "",              DATA_FORMAT, "%02x", DATA_INT, flags,
            "event_type",   "",              DATA_FORMAT, "%02x", DATA_INT, event_type,
            "mic",          "Integrity",     DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "counter",
        "flags",
        "event_type",
        "mic",
        NULL,
};

r_device const vivint = {
        .name        = "Vivint Door/Window Sensor, V-DW21R-345",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 150,
        .long_width  = 0,
        .reset_limit = 300,
        .decode_fn   = &vivint_decode,
        .fields      = output_fields,
};
