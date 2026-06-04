/** @file
    Venduro Extractor Hood protocol.

    Copyright (C) 2026 wlcrs <github.com/wlcrs>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Venduro Extractor Hood protocol.

Bidirectional FSK communication using a TI CC1101 or similar transceiver.
Frequency 868.3 MHz, modulation FSK/NRZ at approx 64 kbps (bit width ~15.5 us).

Packet structure (CC1101 hardware format):

    PPPP...PP SSSS [SSSS] LL HH MMMMMMMM DDDDDDDD QQ CCCCCCCC XXXX

- P: Preamble (10101010...)
- S: Sync word 0xD391, sometimes repeated as 0xD391D391
- L: 1-byte length (typically 0x0E = 14)
- H: 1-byte header/type (0x46)
- M: 4-byte source MAC
- D: 4-byte destination MAC
- Q: 1-byte sequence counter (rolling)
- C: 4-byte command
- X: 2-byte CRC-16, poly 0x8005, init 0xFFFF, over length + payload bytes

Remote -> Hood command bytes [11..14]: 00 27 5A CMD
Hood -> Remote ACK bytes [11..14]:     00 4E 85 STATE
*/
static int venduro_hood_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows == 0)
        return DECODE_ABORT_EARLY;

    unsigned row = 0;
    uint8_t const sync_word[] = {0xD3, 0x91};
    unsigned offset = bitbuffer_search(bitbuffer, row, 0, sync_word, 16);

    if (offset >= bitbuffer->bits_per_row[row])
        return DECODE_ABORT_EARLY;

    // The CC1101 hardware often repeats the sync word. Skip ahead to the true payload.
    uint8_t tmp[2];
    while (offset + 32 <= bitbuffer->bits_per_row[row]) {
        bitbuffer_extract_bytes(bitbuffer, row, offset + 16, tmp, 16);
        if (tmp[0] != 0xD3 || tmp[1] != 0x91)
            break;
        offset += 16;
    }
    offset += 16; // Step past the final sync word

    // Ensure we have at least the length byte
    if (offset + 8 > bitbuffer->bits_per_row[row])
        return DECODE_ABORT_LENGTH;

    uint8_t b_len[1];
    bitbuffer_extract_bytes(bitbuffer, row, offset, b_len, 8);
    uint8_t payload_len = b_len[0];

    // Total required bits: Length byte (1) + Payload (payload_len) + CRC (2)
    unsigned total_bytes = 1 + payload_len + 2;
    unsigned total_bits  = total_bytes * 8;

    if (offset + total_bits > bitbuffer->bits_per_row[row])
        return DECODE_ABORT_LENGTH;

    uint8_t b[32]; // Max CC1101 FIFO is usually 64, 32 is plenty
    if (total_bytes > sizeof(b))
        return DECODE_ABORT_LENGTH;

    bitbuffer_extract_bytes(bitbuffer, row, offset, b, total_bits);

    // Verify CRC-16 over length byte + payload bytes
    uint16_t checksum = crc16(b, payload_len + 1, 0x8005, 0xFFFF);
    uint16_t msg_crc  = (b[payload_len + 1] << 8) | b[payload_len + 2];

    if (checksum != msg_crc) {
        decoder_log(decoder, 1, __func__, "CRC check failed");
        return DECODE_FAIL_MIC;
    }

    char src_mac_str[12];
    char dst_mac_str[12];
    char raw_cmd_str[9];
    char action_buf[64];
    char const *action_str = "Unknown";
    char const *direction  = "Unknown";
    int is_ack             = 0;
    int fan_level          = 0;
    int filter_alarm       = 0;

    // Typical Venduro Remote -> Hood command prefix is 00 27 5A
    if (b[11] == 0x00 && b[12] == 0x27 && b[13] == 0x5A) {
        direction = "Tx";
        if (b[14] == 0x00) {
            action_str = "Button Released";
        }
        else {
            size_t pos = 0;
            action_buf[0] = '\0';

            if (b[14] & 0x20) {
                pos += snprintf(action_buf + pos, sizeof(action_buf) - pos, "%sLight Down", pos ? "+" : "");
            }
            if (b[14] & 0x10) {
                pos += snprintf(action_buf + pos, sizeof(action_buf) - pos, "%sLight Up", pos ? "+" : "");
            }
            if (b[14] & 0x08) {
                pos += snprintf(action_buf + pos, sizeof(action_buf) - pos, "%sLight Toggle", pos ? "+" : "");
            }
            if (b[14] & 0x04) {
                pos += snprintf(action_buf + pos, sizeof(action_buf) - pos, "%sFan Down", pos ? "+" : "");
            }
            if (b[14] & 0x02) {
                pos += snprintf(action_buf + pos, sizeof(action_buf) - pos, "%sFan Up", pos ? "+" : "");
            }
            if (b[14] & 0x01) {
                pos += snprintf(action_buf + pos, sizeof(action_buf) - pos, "%sFan Off", pos ? "+" : "");
            }

            action_str = action_buf[0] ? action_buf : "Unknown";
        }
    }
    // Typical Venduro Hood -> Remote ACK prefix is 00 4E 85
    else if (b[11] == 0x00 && b[12] == 0x4E && b[13] == 0x85) {
        direction  = "ACK";
        action_str = "Status Acknowledgment";
        is_ack     = 1;

        // Parse the "Fill Bar" LED bits (lower 4 bits)
        uint8_t fan_bits = b[14] & 0x0F;
        if (fan_bits == 0x01) {
            fan_level = 1; // 0001
        }
        else if (fan_bits == 0x03) {
            fan_level = 2; // 0011
        }
        else if (fan_bits == 0x07) {
            fan_level = 3; // 0111
        }
        else if (fan_bits == 0x0F) {
            fan_level = 4; // 1111
        }
        else {
            fan_level = 0; // Off or unknown
        }

        // Parse the 5th bit for the Filter Replacement Alarm
        filter_alarm = (b[14] >> 4) & 0x01;
    }

    snprintf(src_mac_str, sizeof(src_mac_str), "%02X%02X%02X%02X", b[2], b[3], b[4], b[5]);
    snprintf(dst_mac_str, sizeof(dst_mac_str), "%02X%02X%02X%02X", b[6], b[7], b[8], b[9]);
    snprintf(raw_cmd_str, sizeof(raw_cmd_str), "%02X%02X%02X%02X", b[11], b[12], b[13], b[14]);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",          DATA_STRING, "Venduro-Hood",
            "type",         "",          DATA_INT,    b[1],
            "direction",    "",          DATA_STRING, direction,
            "src_mac",      "",          DATA_STRING, src_mac_str,
            "dst_mac",      "",          DATA_STRING, dst_mac_str,
            "sequence_num", "",          DATA_INT,    b[10],
            "action",       "",          DATA_STRING, action_str,
            "raw_command",  "",          DATA_STRING, raw_cmd_str,
            "mic",          "Integrity", DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    // Append UI state fields for ACK packets
    if (is_ack) {
        data = data_int(data, "fan_level",    "", NULL, fan_level);
        data = data_int(data, "filter_alarm", "", NULL, filter_alarm);
    }

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "direction",
        "src_mac",
        "dst_mac",
        "sequence_num",
        "action",
        "raw_command",
        "mic",
        "fan_level",
        "filter_alarm",
        NULL,
};

r_device const venduro_hood = {
        .name        = "Venduro Extractor Hood",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 16,
        .long_width  = 16,
        .reset_limit = 600,
        .decode_fn   = &venduro_hood_decode,
        .fields      = output_fields,
};
