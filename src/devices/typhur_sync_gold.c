/** @file
    Typhur Sync Gold meat thermometer probe (Dual/Quad variants).

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Typhur Sync Gold meat thermometer probe (Dual/Quad variants).

Reverse engineered in issue #3138 by \@hakong and \@kevin-david, with
guidance from \@zuckschwerdt. FSK_PCM at 12.5 us/bit, long 0xaa
preamble, 16 bit sync word `0x5754`, 24 byte payload:

    ID:24h ?:8h STATUS:8h ?:8h T1:16h T2:16h T3:16h T4:16h T5:16h
    AMBIENT:16h BATTERY:16h COUNTER:16h CRC:16h

- ID: 24 bit, one per physical probe
- STATUS: bit 3 set when the probe is seated in its charging base
- T1-T5: probe temperature sensors, little-endian, scale 0.01 C
- AMBIENT: little-endian, scale 0.1 C (coarser resolution than T1-T5)
- BATTERY: little-endian, scale 0.01 V
- COUNTER: little-endian, increments every transmission
- CRC: CRC-16 poly 0x8005 init 0x0000 over the preceding 22 bytes

Independently verified against 19 real captures across two physical
probes (issue #3138's attached zip): CRC valid on all 19, and the
decoded values are physically sane -- T1-T5 cluster in a narrow range
per probe (matching the reporter's own "~140-165 F" note), AMBIENT
matches the reporter's "~210-230 F" note, battery voltage is stable
per probe, and COUNTER increments monotonically per probe ID across
the capture sequence.
*/

#define TYPHUR_SYNC_GOLD_PAYLOAD_LEN 24

static uint8_t const typhur_sync_gold_sync[2] = {0x57, 0x54};

static int typhur_sync_gold_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    for (int row = 0; row < bitbuffer->num_rows; row++) {
        int pos = bitbuffer_search(bitbuffer, row, 0, typhur_sync_gold_sync, 16);
        if (pos >= bitbuffer->bits_per_row[row]) {
            continue;
        }
        pos += 16;

        if (bitbuffer->bits_per_row[row] - pos < TYPHUR_SYNC_GOLD_PAYLOAD_LEN * 8) {
            continue;
        }

        uint8_t b[TYPHUR_SYNC_GOLD_PAYLOAD_LEN];
        bitbuffer_extract_bytes(bitbuffer, row, pos, b, TYPHUR_SYNC_GOLD_PAYLOAD_LEN * 8);

        uint16_t crc = crc16(b, 22, 0x8005, 0x0000);
        uint16_t crc_recv = (b[22] << 8) | b[23];
        if (crc != crc_recv) {
            decoder_logf(decoder, 1, __func__, "CRC invalid %04x != %04x", crc, crc_recv);
            continue;
        }

        uint32_t id = ((uint32_t)b[0] << 16) | (b[1] << 8) | b[2];
        int in_base = (b[4] & 0x08) != 0;
        float temp_1_c   = (b[6] | (b[7] << 8)) * 0.01f;
        float temp_2_c   = (b[8] | (b[9] << 8)) * 0.01f;
        float temp_3_c   = (b[10] | (b[11] << 8)) * 0.01f;
        float temp_4_c   = (b[12] | (b[13] << 8)) * 0.01f;
        float temp_5_c   = (b[14] | (b[15] << 8)) * 0.01f;
        float ambient_c  = (b[16] | (b[17] << 8)) * 0.1f;
        float battery_v  = (b[18] | (b[19] << 8)) * 0.01f;
        int counter       = b[20] | (b[21] << 8);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, "Typhur-SyncGold",
                "id",           "",             DATA_FORMAT, "%06x", DATA_INT, id,
                "in_base",      "In base",      DATA_INT,    in_base,
                "counter",      "Counter",      DATA_INT,    counter,
                "battery_V",    "Battery",      DATA_FORMAT, "%.2f V", DATA_DOUBLE, battery_v,
                "temperature_1_C", "Probe 1",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_1_c,
                "temperature_2_C", "Probe 2",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_2_c,
                "temperature_3_C", "Probe 3",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_3_c,
                "temperature_4_C", "Probe 4",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_4_c,
                "temperature_5_C", "Probe 5",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_5_c,
                "ambient_C",    "Ambient",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, ambient_c,
                "mic",          "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return DECODE_FAIL_MIC;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "in_base",
        "counter",
        "battery_V",
        "temperature_1_C",
        "temperature_2_C",
        "temperature_3_C",
        "temperature_4_C",
        "temperature_5_C",
        "ambient_C",
        "mic",
        NULL,
};

r_device const typhur_sync_gold = {
        .name        = "Typhur Sync Gold meat thermometer probe",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 13,
        .long_width  = 13,
        .reset_limit = 3000,
        .decode_fn   = &typhur_sync_gold_decode,
        .fields      = output_fields,
};
