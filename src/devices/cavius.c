/** @file
    Cavius smoke, heat and water detector.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Cavius smoke, heat and water detector decoder.

The alarm units use HopeRF RF69 chips on 869.67 MHz, FSK modulation, 4800 bps.
They seem to use 'Cavi' as a sync word on the chips.
Everything after the sync word is Manchester coded.
The unpacked payload is 11 bytes long structured as follows:

    NNNNMMCSSSS

- N: Network ID (Device ID of the Master device)
- M: Message bytes. Second byte is the first byte inverted (0xFF ^ M)
- C: CRC-8 (Maxim type) of NNNNMM (the first 6 bytes in the payload)
- S: Sending device ID

Message bits as far as we can tell:

- 0x80: PAIRING
- 0x40: TEST
- 0x20: ALARM
- 0x10: WARNING
- 0x08: BATTLOW
- 0x04: MUTE
- 0x02: UNKNOWN2
- 0x01: UNKNOWN1

Sometimes the receiver samplerate has to be at 250ksps to decode properly.
*/

#include "decoder.h"

static int cavius_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x43, 0x61, 0x76, 0x69};

    enum cavius_message {
        cavius_pairing  = 0x80,
        cavius_test     = 0x40,
        cavius_alarm    = 0x20,
        cavius_warning  = 0x10,
        cavius_battlow  = 0x08,
        cavius_mute     = 0x04,
        cavius_unknown2 = 0x02,
        cavius_unknown1 = 0x01,
    };

    // Find the sync
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8);
    if (bit_offset + 22 * 8 >= bitbuffer->bits_per_row[0]) { // Did not find a big enough package
        return DECODE_ABORT_EARLY;
    }
    bit_offset += sizeof(preamble) * 8; // skip sync

    bitbuffer_t databits = {0};

    bitbuffer_manchester_decode(bitbuffer, 0, bit_offset, &databits, 11 * 8);
    bitbuffer_invert(&databits);

    // we require 11 bytes
    if (databits.bits_per_row[0] < 11 * 8) {
        return DECODE_FAIL_SANITY; // manchester_decode fail
    }

    uint8_t *b = databits.bb[0];

    int crc = crc8le(b, 7, 0x31, 0x0);
    if (crc != 0) {
        return DECODE_FAIL_MIC; // invalid CRC
    }

    uint32_t net_id    = ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3]);
    uint32_t sender_id = ((uint32_t)b[7] << 24) | (b[8] << 16) | (b[9] << 8) | (b[10]);
    int batt_low       = (b[4] & cavius_battlow) != 0;
    int message        = (b[4] & ~cavius_battlow); // exclude batt_low bit

    char const *text = batt_low ? "Battery low" : "Unknown";
    switch (message) {
    case cavius_alarm:
        text = "Fire alarm";
        break;
    case cavius_mute:
        text = "Alarm muted";
        break;
    case cavius_pairing:
        text = "Pairing";
        break;
    case cavius_test:
        text = "Test alarm";
        break;
    case cavius_warning:
        text = "Warning/Water detected";
        break;
    default:
        break;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "Cavius-Security",
            "id",           "Device ID",    DATA_INT,    sender_id,
            "battery_ok",   "Battery",      DATA_INT,    !batt_low,
            "net_id",       "Net ID",       DATA_INT,    net_id,
            "message",      "Message",      DATA_INT,    message,
            "text",         "Description",  DATA_STRING, text,
            "mic",          "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "net_id",
        "message",
        "text",
        "mic",
        NULL,
};

r_device const cavius = {
        .name        = "Cavius smoke, heat and water detector",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 206,
        .long_width  = 206,
        .sync_width  = 2700,
        .gap_limit   = 1000,
        .reset_limit = 1000,
        .decode_fn   = &cavius_decode,
        .fields      = output_fields,
};
