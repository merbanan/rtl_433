
/*
 * Honda FR-V Remote Key Fob
 *
 * Copyright (C) 2026 Roman Vohradnik <rovocz@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"



/**
Honda car remote key fob, 433.92 MHz FSK PWM, rolling code.

Preamble: 324 bits of 1s, followed by 8.x or 7.x data bytes.
The last partial byte is always truncated (5 bits received).

Data layout:
    Byte 0:   0x06             fixed sync
    Byte 1:   0x30             fixed sync
    Byte 2:   [CNT:4][0xC:4]   rolling counter (high nibble, decrements each press)
    Byte 3:   0x82             fixed (device ID high)
    Byte 4:   0x3B             fixed (device ID mid)
    Byte 5:   0x3A             fixed (device ID low)
    Byte 6:   rolling byte 1
    Byte 7:   rolling byte 2   (LOCK only, may be partial)
    Byte 8:   0xA8             (LOCK only, always truncated to 5 bits = 0x10101)

Command detection by total data bit count:
    LOCK:   69 bits (8 full bytes + 5 partial bits of 0xA8)
    UNLOCK: 61 bits (7 full bytes + 5 partial bits)

Rolling counter decrements by 1 each press (shared across LOCK/UNLOCK).

Example:
    06 30 cc 82 3b 3a cf cf [a8]  -> LOCK   counter=0xc  rolling=0xcfcf
    06 30 9c 82 3b 3a b2 [08]     -> UNLOCK counter=0x9  rolling=0xb2??
*/

#include "decoder.h"

static int honda_frv_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    for (int row = 0; row < bitbuffer->num_rows; row++) {
        int total_bits = bitbuffer->bits_per_row[row];

        // Need preamble (324b) + minimally 61 data bits
        if (total_bits < 324 + 61) {
            continue;
        }

        uint8_t *bb = bitbuffer->bb[row];

        // Find end of pereamble
        int data_start = -1;
        for (int i = 0; i < total_bits - 48; i++) {
            if (!((bb[i / 8] >> (7 - (i % 8))) & 1)) {
                data_start = i;
                break;
            }
        }

        if (data_start < 0) {
            continue;
        }

        int data_bits = total_bits - data_start;

        // LOCK = 69 bits, UNLOCK = 61 bits (tolerance ±3)
        const char *cmd;
        if (data_bits >= 66 && data_bits <= 72) {
            cmd = "LOCK";
        } else if (data_bits >= 58 && data_bits <= 64) {
            cmd = "UNLOCK";
        } else {
            continue;
        }

        // Extract 8 data bytes (byte[7] can be only partialy filled for unlock)
        uint8_t data[8] = {0};
        int extract = (data_bits >= 64) ? 64 : data_bits;  // max 8 bytes
        for (int i = 0; i < extract; i++) {
            int src = data_start + i;
            int bit = (bb[src / 8] >> (7 - (src % 8))) & 1;
            data[i / 8] |= (bit << (7 - (i % 8)));
        }

        // Validation fix bytes
        if (data[0] != 0x06 && data[0] != 0x05) {
            continue;
        }
        if ((data[2] & 0x0F) != 0x0C) {
            continue;
        }

        uint32_t device_id = ((uint32_t)data[3] << 16)
                           | ((uint32_t)data[4] <<  8)
                           |  (uint32_t)data[5];
        uint16_t counter   = ((uint16_t)data[1] << 4) | (data[2] >> 4);

        // Rolling: for LOCK bytes[6,7], pro UNLOCK only byte[6]
        // (bajt[7] for unlock is striped)
        uint16_t rolling = (uint16_t)data[6] << 8;
        if (data_bits >= 64) {
            rolling |= data[7];
        }

        /* clang-format off */
        data_t *d = data_make(
                "model",   "",           DATA_STRING, "Honda_FRV",
                "id",      "Device ID",  DATA_FORMAT, "%06x", DATA_INT, device_id,
                "cmd",     "Command",    DATA_STRING, cmd,
                "counter", "Counter",    DATA_INT,    counter,   // DATA_INT zvládne uint16_t
                "rolling", "Rolling",    DATA_FORMAT, "%04x", DATA_INT, rolling,
                "mic",     "Integrity",  DATA_STRING, "NONE",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, d);
        return 1;
    }

    return DECODE_ABORT_EARLY;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "cmd",
        "counter",
        "rolling",
        "mic",
        NULL,
};

r_device const honda_frv = {
        .name        = "Honda_FRV",
        .modulation  = FSK_PULSE_PWM,
        .short_width = 248,
        .long_width  = 492,
        .reset_limit = 1600,
        .gap_limit   = 0,
        .sync_width  = 0,
        .tolerance   = 60,
        .decode_fn   = &honda_frv_decode,
        .fields      = output_fields,
};

