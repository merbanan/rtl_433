/*
 * Suzuki SX4 Remote Key Fob
 *
 * Copyright (C) 2026 Roman Vohradnik <rovocz@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


/**
Suzuki car remote key fob, 433.92 MHz FSK PWM, rolling code (KeeLoq).

Signal transmitted 4x per button press. 144 bits per packet.
Trailing bytes 9-17 are always 0xFF.

Data layout:
  Byte 0:    0x14        fixed family ID
  Byte 1:    0x01/0x02   command: 0x01=LOCK, 0x02=UNLOCK
  Byte 2:    0x1E        fixed device ID
  Byte 3:    0x3F        fixed device ID
  Byte 4:    0x8C        fixed device ID
  Bytes 5-8: KeeLoq rolling code (32 bits, changes every press)
  Bytes 9-17: 0xFF       trailing

Device ID: bytes 2,3,4 = 1E 3F 8C

Examples:
  14 01 1e 3f 8c 21 fd 19 67 ff...  -> LOCK
  14 02 1e 3f 8c 08 e0 a1 48 ff...  -> UNLOCK
*/


#include "decoder.h"
#include <time.h>

static uint32_t suzuki_last_rolling = 0;
static time_t   suzuki_last_time    = 0;

static int suzuki_sx4_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int events = 0;

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        if (bitbuffer->bits_per_row[row] != 144) {
            continue;
        }

        uint8_t *b = bitbuffer->bb[row];

        if (b[0] != 0x14) continue;
//        if (b[2] != 0x1E) continue;
//        if (b[3] != 0x3F) continue;
//        if (b[4] != 0x8C) continue;

        const char *cmd;
        if (b[1] == 0x01) {
            cmd = "LOCK";
        } else if (b[1] == 0x02) {
            cmd = "UNLOCK";
        } else {
            continue;
        }

        uint32_t rolling = ((uint32_t)b[5] << 24)
                         | ((uint32_t)b[6] << 16)
                         | ((uint32_t)b[7] <<  8)
                         |  (uint32_t)b[8];

        // Deduplication – same rolling code is ignored for 2 seconds
        time_t now = time(NULL);
        if (rolling == suzuki_last_rolling && (now - suzuki_last_time) < 2) {
            break;
        }
        suzuki_last_rolling = rolling;
        suzuki_last_time    = now;

        char id_str[7];
        snprintf(id_str, sizeof(id_str), "%02x%02x%02x", b[2], b[3], b[4]);

        char rolling_str[9];
        snprintf(rolling_str, sizeof(rolling_str), "%08x", rolling);

        /* clang-format off */
        data_t *data = data_make(
                "model",   "",          DATA_STRING, "Suzuki-SX4",
                "id",      "Device ID", DATA_STRING, id_str,
                "cmd",     "Command",   DATA_STRING, cmd,
                "rolling", "Rolling",   DATA_STRING, rolling_str,
                "mic",     "Integrity", DATA_STRING, "NONE",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        events++;
        break;
    }

    return events > 0 ? events : DECODE_ABORT_EARLY;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "cmd",
        "rolling",
        "mic",
        NULL,
};

r_device const suzuki_sx4 = {
        .name        = "Suzuki-SX4",
        .modulation  = FSK_PULSE_PWM,
        .short_width = 256,
        .long_width  = 512,
        .reset_limit = 600,
        .gap_limit   = 0,
        .sync_width  = 0,
        .tolerance   = 80,
        .decode_fn   = &suzuki_sx4_decode,
        .fields      = output_fields,
};

