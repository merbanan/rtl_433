/** @file
    Quinetic Switches and Sensors.

    Copyright (C) 2024 Nick Parrott

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Quinetic Switches and Sensors.

## Frame Layout

    ...PPPP SS IISCC

- P: 48-bits+ of Preamble
- S: 16-bits of Sync-Word (0xA4, 0x23)
- I: 16-bits of Device ID
- S: 8-bits of Device Action
- C: 16-bits of In-Packet Checksum (CRC-16 AUG-CCITT)

## CRC Checksum Method

- In-Packet Checksum: CC
- 24-bits of data to CRC-check: IIS

## Signal Summary

- Frequency: 433.3 Mhz, +/- 50Khz
- Nominal pulse width: 10us
- Modulation: FSK_PCM
- Checksum: CRC-16/AUG-CCITT

## Device Characteristics

- A switch emits 3-4 pulses when button is pressed.
- A switch emits 3-4 pulses when button is released.
- This duplication of packets is expected.
- Device ID is preserved as 16-bit Hex.
- It is printed on device rear-label (some models).
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

    uint8_t b[5];
    bitbuffer_extract_bytes(bitbuffer, 0, syncword_bitindex + 16, b, sizeof(b) * 8);

    int crc = crc16(b, 5, 0x1021, 0x1D0F);
    if (crc != 0) {
        decoder_logf(decoder, 1, __func__, "CRC failure");
        return DECODE_FAIL_MIC;
    }

    // Process Switch-Channel (Button) nibble: b[2]
    //
    // Determine button number in switch (B1/B2/B3) when pressed.
    // Typical Int Values:
    //
    // 192 = generic release
    // 01 = press ( B1 )
    // 02 = press ( B2 )
    // 03 = press ( B3 )
    int switch_channel = b[2];
    if (switch_channel == 192) {
        // Ignore "button release": button number unknown.
        return DECODE_ABORT_EARLY;
    }

    // Process Switch-ID nibbles: b[0] and b[1]
    int id = (b[0] << 8) | (b[1]);

    /* clang-format off */
    data_t *data = data_make(
        "model",         "Model",           DATA_STRING, "Quinetic",
        "id",            "ID",              DATA_FORMAT, "%04x", DATA_INT, id,
        "channel",       "Channel",         DATA_INT,    switch_channel,
        "mic",           "Integrity",       DATA_STRING, "CRC",
        NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channnel",
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
