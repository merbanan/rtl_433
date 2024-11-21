/** @file
    Quinetic Switches and Sensors

    Copyright (C) 2024 Nick Parrott

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
################################
Frame Layout

...PPPP SS IISCC

P: 48-bits+ of Preamble
S: 16-bits of Sync-Word (0xA4, 0x23)
I: 16-bits of Device ID
S: 8-bits of Device Action
C: 16-bits of In-Packet Checksum (CRC-16 AUG-CCITT)

################################
CRC Checksum Method

    In-Packet Checksum: CC
    24-bits of data to CRC-check: IIS

################################
Signal Summary

Frequency: 433.3 Mhz, +/- 50Khz
Nominal pulse width: 10us
Modulation: FSK_PCM
Checksum: CRC-16/AUG-CCITT

################################
Device Characteristics

A switch emits 3-4 pulses when button is pressed.
A switch emits 3-4 pulses when button is released.
This duplication of packets is expected.

Device ID is preserved as 16-bit Hex.
It is printed on device rear-label (some models).
*/

#include "decoder.h"

static int quinetic_switch_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{

    if (bitbuffer->bits_per_row[0] < 110 || bitbuffer->bits_per_row[0] > 140) {
        return DECODE_ABORT_LENGTH;
    }

    const uint8_t packet_syncword[] = {0xA4, 0x23};
    unsigned syncword_bitindex;

    syncword_bitindex = bitbuffer_search(bitbuffer, 0, 0, packet_syncword, 16);
    if (syncword_bitindex >= bitbuffer->bits_per_row[0]) {
        decoder_logf(decoder, 1, __func__, "Sync-Word not found");
        return DECODE_ABORT_EARLY;
    }

    // DEBUG
    // decoder_logf(decoder, 1, __func__, "Sync-Word Index: %d", syncword_bitindex);
    // decoder_logf(decoder, 1, __func__, "Bits in Row: %d", bitbuffer->bits_per_row[0]);

    uint8_t payload[5];
    bitbuffer_extract_bytes(bitbuffer, 0, syncword_bitindex + 16, payload, sizeof(payload) * 8);
    
    char checksum_data_str[24];
    snprintf(checksum_data_str, sizeof(checksum_data_str), "%02x%02x%02x", payload[0], payload[1], payload[2]);
    
    char checksum_str[16];
    snprintf(checksum_str, sizeof(checksum_str), "%02x%02x", payload[3], payload[4]);

    uint16_t crc;
    uint8_t checksum_data[] = {payload[0], payload[1], payload[2]};
    crc = crc16(checksum_data, 3, 0x1021, 0x1D0F);
    
    char checksum_crc[16];
    snprintf(checksum_crc, sizeof(checksum_crc), "%x", crc);
    if ( strcmp(checksum_crc, checksum_str) != 0 ) {
        decoder_logf(decoder, 1, __func__, "Checksum failed. Expected: %s, Calculated: %s.", checksum_str, checksum_crc);
        return DECODE_FAIL_MIC;
    }

    char id_hex[16];
    snprintf(id_hex, sizeof(id_hex), "%02X%02X", payload[0], payload[1]);
    // uint16_t id_int = (payload[0] << 8 | payload[1]);
    
    char action_str[] = "release";
    if ((payload[2] >> 7) == 0) {
        strcpy(action_str, "press");
    }

    /* clang-format off */
    data_t *data = data_make(
        "model",       "Model",           DATA_STRING, "Quinetic",
        "id",          "ID",              DATA_STRING, id_hex,
        "action",      "Action",          DATA_STRING, action_str,
        "action_int",  "Action Integer",  DATA_INT,    payload[2],
        "checksum",    "In-Pkt Checksum", DATA_STRING, checksum_str,
        "checksumdata", "Checksum Data",  DATA_STRING, checksum_data_str,
        "mic",         "Integrity",       DATA_STRING, "CRC",
        NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "action",
        "action_int",
        "checksum",
        "checksumdata",
        "mic",
        NULL,
};

r_device const quinetic = {
        .name        = "Quinetic",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 10,
        .long_width  = 10,
        .reset_limit = 120,
        .tolerance   = 1,
        .decode_fn   = &quinetic_switch_decode,
        .fields      = output_fields,
        .disabled    = 1, // disabled by default, due to required settings: frequency 433.4, sample_rate 1024k
};
