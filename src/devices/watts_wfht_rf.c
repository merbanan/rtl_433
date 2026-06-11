/** @file
    Watts WFHT-LCD-RF wireless underfloor heating thermostat.

    Copyright (C) 2026 - Steven Bontius - https://github.com/StevenBontius
    
    Reverse engineering document: https://github.com/StevenBontius/watts-wfht-ha/blob/main/docs/WATTS_WFHT_RF_PROTOCOL.md

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Watts WFHT LCD RF decoder

This decoder is independent of the existing watts_thermostat decoder, which
targets a different Watts hardware generation.

Modulation: OOK, Manchester encoded (zero-bit convention).
Bitrate:    approximately 1085 bps (chip rate ~2170 chips/s).
Frequency:  433.92 MHz nominal. Devices transmit at roughly +73 kHz offset,
            so they are also receivable when tuned to 434.0 MHz.

Frame layout (24 bytes, 192 bits):

    Byte    Content
    ----    -------
     0-3    Preamble 2A AA AA AA
     4-7    Sync word D3 91 D3 91
     8      Length 0x0D (13 bytes follow)
     9-11   Protocol header FF FF FE (constant across all observed traffic)
    12      Mode/state bitfield. Two bits identified and exposed:
              bit 0 (0x01): pairing-in-progress flag. Set to 1 while the
                            thermostat is in its pairing handshake state.
                            On a Basic LCD model this state is entered by
                            holding the OK button until "RF" appears on the
                            display, and exited via the "End" menu item; the
                            broadcasts during this window carry bit 0 = 1
                            so the receiver can recognize them as pairing
                            traffic rather than normal operational data.
                            During normal operation the bit is 0. freq 2 Hz
              bit 1 (0x02): heat/cool selection (1 = heat, 0 = cool).
                            Reflects the thermostat's local J1 setting.
              bits 2-7:     unobserved as non-zero in normal operation;
                            semantics unknown.
            Output fields: mode ("heat" or "cool") and pairing ("true" or
            "false").
    13-15   Device ID, 3 bytes (the on-air device identifier)
    16-17   Ambient temperature, 16-bit big-endian, scale /10 for degrees C
    18-19   Target setpoint, 16-bit big-endian, scale /10 for degrees C
    20      Call-for-heat output. The thermostat runs an on-board
            chronoproportional P controller and writes its 0-100 duty
            cycle into this byte; only the endpoints are ever observed at
            the wire (0x00 = idle, 0x64 = 100 = calling) because the duty
            is realised as an on/off time pattern across a 15-minute cycle
            rather than as intermediate byte values. Exposed as the
            call_for_heat field with values 0 or 100. Note that in cool
            mode (byte 12 bit 1 = 0) the same wire-level state means
            "call for cool"; the field name reflects the conventional
            heating direction but the underlying bit is direction-agnostic.
    21      CRC-8 application checksum (poly 0xE6, init 0, XOR 0xBE then
            XOR byte 20). Covers bytes 8..19.
    22-23   CRC-16/CMS radio checksum (poly 0x8005, init 0xFFFF, no
            reflection, xorout 0x0000). Covers bytes 8..21. On a normal
            CC1101 receiver this is verified and stripped in hardware;
            here we receive the raw bits and validate it ourselves.

Setpoint-change A-B-A burst
---------------------------
When the user changes the setpoint on a thermostat, the thermostat emits a
rapid three-frame burst with the following structure:

    Frame   T+ms      Setpoint field    CRC-8
    -----   -------   ---------------   -----
    A       0         real value        hash_A
    B       ~400      real value + 0.1  hash_B
    A       ~800      real value        hash_A

The decoder emits all three frames faithfully. Downstream consumers that
want a clean setpoint stream may wish to median-filter or debounce three
consecutive frames from the same device ID within a ~1 second window.

Confirmed via captures: device ID, ambient temperature, target setpoint,
frame structure, both CRC algorithms, the A-B-A setpoint-change burst
behavior, the pairing and heat/cool bits within byte 12, and the
call-for-heat semantics of byte 20.

A 193rd bit may appear on the end of the frame as a Manchester alignment
artifact and is harmless: this decoder reads a fixed 128 bits after the
sync word and ignores anything after.
*/

#include "decoder.h"

static uint8_t watts_wfht_rf_crc8(uint8_t const *data, int len, uint8_t b20)
{
    uint8_t crc = 0x00;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xE6) : (uint8_t)(crc << 1);
        }
    }
    return (uint8_t)(crc ^ 0xBE ^ b20);
}

static uint16_t watts_wfht_rf_crc16(uint8_t const *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x8005) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static int watts_wfht_rf_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const sync_pattern[4] = {0xD3, 0x91, 0xD3, 0x91};

    int events = 0;

    for (unsigned row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned const row_len = bitbuffer->bits_per_row[row];

        if (row_len < 32 + 128) {
            continue;
        }

        unsigned search_start = 0;
        while (search_start + 32 + 128 <= row_len) {
            unsigned bit_offset = bitbuffer_search(bitbuffer, row, search_start,
                    sync_pattern, 32);

            if (bit_offset + 32 + 128 > row_len) {
                break;
            }

            search_start = bit_offset + 32;

            /* Extract 16 bytes after the sync word. These correspond to
               original packet bytes 8..23:
                 b[0]      = length byte 0x0D
                 b[1..3]   = protocol header FF FF FE
                 b[4]      = mode byte (original byte 12)
                 b[5..7]   = device id (original bytes 13..15)
                 b[8..9]   = ambient temperature, BE (original bytes 16..17)
                 b[10..11] = setpoint temperature, BE (original bytes 18..19)
                 b[12]     = call-for-heat byte (original byte 20)
                 b[13]     = CRC-8 (original byte 21)
                 b[14..15] = CRC-16 (original bytes 22..23)
            */
            uint8_t b[16];
            bitbuffer_extract_bytes(bitbuffer, row, bit_offset + 32, b, 128);

            if (b[0] != 0x0D) {
                decoder_logf(decoder, 2, __func__,
                        "length byte mismatch: got 0x%02X, expected 0x0D",
                        b[0]);
                continue;
            }

            if (b[1] != 0xFF || b[2] != 0xFF || b[3] != 0xFE) {
                decoder_logf(decoder, 2, __func__,
                        "protocol header mismatch: %02X %02X %02X",
                        b[1], b[2], b[3]);
                continue;
            }

            uint8_t const crc8_calc = watts_wfht_rf_crc8(b, 12, b[12]);
            if (crc8_calc != b[13]) {
                decoder_logf(decoder, 2, __func__,
                        "CRC-8 fail: calc=0x%02X recv=0x%02X (byte20=0x%02X)",
                        crc8_calc, b[13], b[12]);
                continue;
            }

            uint16_t const crc16_calc = watts_wfht_rf_crc16(b, 14);
            uint16_t const crc16_recv = (uint16_t)(((uint16_t)b[14] << 8) | b[15]);
            if (crc16_calc != crc16_recv) {
                decoder_logf(decoder, 2, __func__,
                        "CRC-16 fail: calc=0x%04X recv=0x%04X",
                        crc16_calc, crc16_recv);
                continue;
            }

            char id_str[16];
            snprintf(id_str, sizeof(id_str), "%02X:%02X:%02X",
                    b[5], b[6], b[7]);

            /* Decoded bits within byte 12 */
            char const *const mode    = (b[4] & 0x02) ? "heat" : "cool";
            char const *const pairing = (b[4] & 0x01) ? "true" : "false";

            /* Call-for-heat as 0 / 100 percent. The wire only ever carries
               0x00 or 0x64 (= 100), even though the internal controller
               state is a 0-100 duty cycle: intermediate duties are realised
               as an on/off time pattern across the 15 minute Cy window
               rather than as intermediate byte values. */
            int const call_for_heat = (b[12] == 0x64) ? 100 : 0;

            int16_t const temp_raw     = (int16_t)(((uint16_t)b[8] << 8) | b[9]);
            int16_t const setpoint_raw = (int16_t)(((uint16_t)b[10] << 8) | b[11]);
            double const temperature_C = temp_raw / 10.0;
            double const setpoint_C    = setpoint_raw / 10.0;

            /* clang-format off */
            data_t *data = data_make(
                    "model",         "",              DATA_STRING, "Watts-WFHT-RF",
                    "id",            "ID",            DATA_STRING, id_str,
                    "mode",          "Mode",          DATA_STRING, mode,
                    "pairing",       "Pairing",       DATA_STRING, pairing,
                    "temperature_C", "Temperature",   DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature_C,
                    "setpoint_C",    "Setpoint",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, setpoint_C,
                    "call_for_heat", "Call for heat", DATA_FORMAT, "%d %%",  DATA_INT,    call_for_heat,
                    "mic",           "Integrity",     DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            events++;
        }
    }

    return events;
}

static char const *const watts_wfht_rf_output_fields[] = {
        "model",
        "id",
        "mode",
        "pairing",
        "temperature_C",
        "setpoint_C",
        "call_for_heat",
        "mic",
        NULL,
};

r_device const watts_wfht_rf = {
        .name        = "Watts WFHT-RF / WFHC-MASTERH&C-RF underfloor heating thermostat",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 460,
        .long_width  = 0,
        .reset_limit = 900,
        .decode_fn   = &watts_wfht_rf_decode,
        .fields      = watts_wfht_rf_output_fields,
};