/** @file
    Cellular Tracking Technologies (CTT) LifeTag/PowerTag/HybridTag.

    Copyright (C) 2025 Jonathan Caicedo <jonathan@jcaicedo.com>
    Credit to https://github.com/tve for the CTT tag implementation details via their work on RadioJay (https://radiojay.org/) and Motus Test Tags (https://github.com/tve/motus-test-tags).

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Cellular Tracking Technologies (https://celltracktech.com/) LifeTag/PowerTag/HybridTag.

CTT LifeTag/PowerTag/HybridTag is a lightweight transmitter used for wildlife tracking and research - most commonly used with the Motus Wildlife Tracking System (https://motus.org/).

The packet format consists of:

    • PREAMBLE: 24 bits of alternating 1/0 (0xAA if byte-aligned) for receiver bit-clock sync
    • SYNC:     2 bytes fixed pattern 0xD3, 0x91 marking the packet start
    • ID:       20-bit tag ID encoded into 4 bytes (5 bits per byte) using a 32-entry dictionary
    • CRC:      1-byte SMBus CRC-8 over the 4 encoded ID bytes

*/

#include "decoder.h"

static const uint8_t sync[2] = {0xD3, 0x91};

static const uint8_t ctt_code[32] = {
    0x00, 0x07, 0x19, 0x1E, 0x2A, 0x2D, 0x33, 0x34,
    0x4B, 0x4C, 0x52, 0x55, 0x61, 0x66, 0x78, 0x7F,
    0x80, 0x87, 0x99, 0x9E, 0xAA, 0xAD, 0xB3, 0xB4,
    0xCB, 0xCC, 0xD2, 0xD5, 0xE1, 0xE6, 0xF8, 0xFF
};

// Simple linear search to find index of codeword
static int dict_index(uint8_t val) {
    for (int i = 0; i < 32; i++) {
        if (ctt_code[i] == val) {
            return i;
        }
    }
    return -1; // Not found
}

static int ctt_tag_decode(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    int events = 0;

    // Expect at least sync + payload (56 bits), but allow extra (e.g., preamble)
    const int min_bits = 56;

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] < min_bits) {
            continue; // Too short
        }

        // Search for sync (allow 0 bit errors initially; increase to 2 for noisy signals)
        unsigned sync_pos = bitbuffer_search(bitbuffer, row, 0, sync, 16); // 2 bytes = 16 bits

        if (sync_pos >= bitbuffer->bits_per_row[row]) {
            continue; // Sync not found
        }

        // Ensure enough bits after sync for ID (4B) + CRC (1B) = 40 bits
        if (sync_pos + 16 + 40 > bitbuffer->bits_per_row[row]) {
            continue;
        }

        // Extract 5 bytes after sync
        uint8_t payload[5];
        bitbuffer_extract_bytes(bitbuffer, row, sync_pos + 16, payload, 40);

        // Extract encoded ID and CRC
        uint8_t enc_id[4];
        memcpy(enc_id, payload, 4);
        uint8_t crc_val = payload[4];

        // SMBus CRC-8
        if (crc8(enc_id, 4, 0x07, 0x00) != crc_val) {
            decoder_logf(decoder, 2, __func__, "CRC fail (calc 0x%02X != rx 0x%02X)", crc8(enc_id, 4, 0x07, 0x00), crc_val);
            continue; // DECODE_FAIL_MIC?
        }

        // Decode ID (20 bits packed in 4x5)
        uint32_t id = 0; // Use uint32_t for safety
        int valid = 1;
        for (int j = 0; j < 4; j++) {
            int idx = dict_index(enc_id[j]);
            if (idx < 0) {
                valid = 0;
                break;
            }
            id |= ((uint32_t)idx << (5 * (3 - j))); // MSB first
        }
        if (!valid) {
            decoder_log(decoder, 2, __func__, "Invalid codeword in encoded ID");
            continue;
        }

        // Format hex representations
        char id_hex[8];
        snprintf(id_hex, sizeof(id_hex), "0x%05X", (unsigned)id);

        char id_raw_hex[16]; // "XX XX XX XX"
        snprintf(id_raw_hex, sizeof(id_raw_hex), "%02X %02X %02X %02X",
                 enc_id[0], enc_id[1], enc_id[2], enc_id[3]);

        /* clang-format off */
        data = data_make(
            "model",      "",                        DATA_STRING, "CTT Motus LifeTag/PowerTag/HybridTag",
            "id_raw",     "Raw Encoded ID",          DATA_STRING, id_raw_hex,
            "id",         "Decoded Tag ID",          DATA_INT,    id,
            "id_hex",     "Decoded Tag ID (hex)",    DATA_STRING, id_hex,
            "crc",        "CRC",                     DATA_FORMAT, "%02X", DATA_INT, crc_val,
            "mic",        "Integrity",               DATA_STRING, "CRC",
            NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        events++;
    }

    return events;
}

static const char *ctt_tag_fields[] = {
    "model", "id_raw", "id", "id_hex", "crc", "mic", NULL
};

r_device const ctt_tag = {
    .name           = "CTT Motus LifeTag/PowerTag/HybridTag",
    .modulation     = FSK_PULSE_PCM,
    /* at BR=25 kbps, bit_time=40µs*/
    .short_width    = 40,
    .long_width     = 40,
    .tolerance      = 10,
    /* allow up to 3×bit for same symbol */
    .gap_limit      = 200,
    .reset_limit    = 50000,  /* 50 ms */
    .decode_fn      = &ctt_tag_decode,
    .fields         = ctt_tag_fields,
    .disabled       = 0, // Set to 1 during development
};
