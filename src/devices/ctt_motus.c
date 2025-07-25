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

The CTT LifeTag/PowerTag/HybridTag is a lightweight transmitter used for wildlife tracking and research - most commonly used with the Motus Wildlife Tracking System (https://motus.org/).
The tags transmit a unique identifier (ID) at a fixed bitrate of 25 kbps using Frequency Shift Keying (FSK) modulation on 434 MHz.

The packet format consists of:

    • PREAMBLE: 24 bits of alternating 1/0 (0xAA if byte-aligned) for receiver bit-clock sync (preamble length can be shorter, depending on hardware)
    • SYNC:     2 bytes fixed pattern 0xD3, 0x91 marking the packet start
    • ID:       20-bit tag ID encoded into 4 bytes (5 bits per byte) using a 32-entry dictionary
    • CRC:      1-byte SMBus CRC-8 over the 4 encoded ID bytes

    AA AA AA   D3 91   78 55 4C 33   58
   |--------| |-----| |-----------| |--|
    Preamble   Sync        ID       CRC

    LifeTag - programmed with a standard 5-second beep rate.
    PowerTag - user-defined beep rate
    HybridTag - transmits every 2-15 seconds



*/

#include "decoder.h"

static const uint8_t sync[2] = {0xD3, 0x91};

static int ctt_motus_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int events = 0;

    // Expect at least sync + payload (56 bits), but allow extra (e.g., preamble)
    const int min_bits = 56;

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] < min_bits) {
            continue; // DECODE_ABORT_LENGTH?
        }

        // Search for sync (allow 0 bit errors initially; increase to 2 for noisy signals)
        unsigned sync_pos = bitbuffer_search(bitbuffer, row, 0, sync, 16); // 2 bytes = 16 bits

        if (sync_pos >= bitbuffer->bits_per_row[row]) {
            continue; // DECODE_ABORT_EARLY?
        }

        // Ensure enough bits after sync for ID (4B) + CRC (1B) = 40 bits
        if (sync_pos + 16 + 40 > bitbuffer->bits_per_row[row]) {
            continue; // DECODE_ABORT_EARLY?
        }

        // Extract 5 bytes after sync
        uint8_t payload[5];
        bitbuffer_extract_bytes(bitbuffer, row, sync_pos + 16, payload, 40);

        // SMBus CRC-8
        if (crc8(payload, 5, 0x07, 0x00) != 0) {
            decoder_logf(decoder, 2, __func__, "CRC fail (calc 0x%02X != rx 0x%02X)", crc8(payload, 5, 0x07, 0x00), 0);
            return DECODE_FAIL_SANITY; // Integrity check failed - no point in continuing
        }

        uint32_t id = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];

        /* clang-format off */
        data = data_make(
            "model",      "",                        DATA_STRING, "CTT - Motus",
            "id",         "Tag ID",                  DATA_FORMAT, "0x%08X", DATA_INT, id,
            "mic",        "Integrity",               DATA_STRING, "CRC",
            NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        events++;
    }

    return events;
}


static const char *output_fields[] = {
        "model",
        "id_raw",
        "id",
        "id_hex",
        "crc",
        "mic",
        NULL,
};

r_device const ctt_motus = {
        .name       = "Cellular Tracking Technologies LifeTag/PowerTag/HybridTag",
        .modulation = FSK_PULSE_PCM,
        /* at BR=25 kbps, bit_time=40µs*/
        .short_width = 40,
        .long_width  = 40,
        .tolerance   = 10,
        .gap_limit   = 200,
        .reset_limit = 50000, /* 50 ms */
        .decode_fn   = &ctt_motus_decode,
        .fields      = output_fields,
        .disabled    = 0,
};
