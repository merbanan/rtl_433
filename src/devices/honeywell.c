/** @file
    Honeywell (Ademco) Door/Window Sensors (345Mhz).

    Copyright (C) 2016 adam1010
    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Honeywell (Ademco) Door/Window Sensors (345.0Mhz).

Tested with the Honeywell 5811 Wireless Door/Window transmitters.

Also: 2Gig DW10 door sensors,
and Resolution Products RE208 (wire to air repeater).
And DW11 with 96 bit packets.

Maybe: 5890PI?

64 bit packets, repeated multiple times per open/close event.

Protocol whitepaper: "DEFCON 22: Home Insecurity" by Logan Lamb.

Data layout:

    PP PP C IIIII EE SS SS

- P: 16bit Preamble and sync bit (always ff fe)
- C: 4bit Channel
- I: 20bit Device serial number / or counter value
- E: 8bit Event, where 0x80 = Open/Close, 0x04 = Heartbeat / or id
- S: 16bit CRC

These are my rough notes for the Vivint DW11 device

Vivint DW11 - 96 Bits

// hh COUNT:16d CON:b TAM:b REED:b ALARM:b BAT:b HEART:b bb ID:32d CRC:hh CRC:hh

    b    0  1 2  3  4 5  6 7  8 9
    PPPP 7A TTTT EE IIII IIII SSSS
    {80} 7a 0091 00 0105 7325 2480

- P: 16bit Preamble and sync bit (always ff fe)
- T: 16bit Count
- E: 8bit Event, where 0x80 = Open/Close, 0x04 = Heartbeat / or id
- I: 32bit Device serial number / or counter value
- S: 16bit CRC

*/

#include "decoder.h"

#define BYTE_TO_BINARY(byte) \
    ((byte)&0x80 ? '1' : '0'), \
            ((byte)&0x40 ? '1' : '0'), \
            ((byte)&0x20 ? '1' : '0'), \
            ((byte)&0x10 ? '1' : '0'), \
            ((byte)&0x08 ? '1' : '0'), \
            ((byte)&0x04 ? '1' : '0'), \
            ((byte)&0x02 ? '1' : '0'), \
            ((byte)&0x01 ? '1' : '0')

static int honeywell_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    decoder_logf(decoder, 0, __func__, "honeywell_decode - start");
    // full preamble is 0xFFFE
    uint8_t const preamble_pattern[2] = {0xff, 0xe0}; // 12 bits

    data_t *data;
    int row;
    int pos;
    int len;
    uint8_t b[10] = {0};
    int channel;
    int device_id;
    int event;
    int count;
    uint16_t crc_calculated;
    uint16_t crc;
    int reed;
    int contact;
    int heartbeat;
    int alarm;
    int tamper;
    int battery_low;

    row = 0; // we expect a single row only. reduce collisions
    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[row] < 60) {
        decoder_logf(decoder, 0, __func__, "DECODE_ABORT_LENGTH rows %d, bits per %d", bitbuffer->num_rows, bitbuffer->bits_per_row[row]);
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_invert(bitbuffer);

    pos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, 12) + 12;
    len = bitbuffer->bits_per_row[row] - pos;
    if (len < 48) {
        decoder_logf(decoder, 0, __func__, "DECODE_ABORT_LENGTH len %d", len);
        return DECODE_ABORT_LENGTH;
    }
    bitbuffer_extract_bytes(bitbuffer, row, pos, b, 80);

    if ((len == 80) && (b[0] = 0x7a)) {
        // DW11
        decoder_logf(decoder, 0, __func__, "Bits 0-%02x 1-%02x 2-%02x 3-%02x 4-%02x 5-%02x 6-%02x 7-%02x 8-%02x 9-%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9]);
        count     = (b[1] << 8) | (b[2]);
        channel   = 2;
        device_id = ((b[4] << 24) | (b[5] << 16) | ((b[6]) << 8) | (b[7])) + 143222784;
        crc       = (b[8] << 8) | b[9];
        event     = b[3];

        decoder_logf(decoder, 0, __func__, "Counter LSB 0-%c%c%c%c%c%c%c%c 1-%c%c%c%c%c%c%c%c 2-%c%c%c%c%c%c%c%c", BYTE_TO_BINARY(b[3]), BYTE_TO_BINARY((b[2] << 2) ^ b[3]), BYTE_TO_BINARY(b[2] ^ b[3]));

        decoder_logf(decoder, 0, __func__, "CRC High 0-%c%c%c%c%c%c%c%c 1-%c%c%c%c%c%c%c%c 2-%c%c%c%c%c%c%c%c", BYTE_TO_BINARY(b[3]), BYTE_TO_BINARY(b[8] ^ b[3]), BYTE_TO_BINARY(b[9] ^ b[3]));

        decoder_logf(decoder, 0, __func__, "CRC Low 0-%c%c%c%c%c%c%c%c 1-%c%c%c%c%c%c%c%c 2-%c%c%c%c%c%c%c%c", BYTE_TO_BINARY(b[3]), BYTE_TO_BINARY((b[8] + b[9]) ^ b[3]), BYTE_TO_BINARY(((b[1] + b[2]) << 2) ^ b[3]));
    }
    else {
        channel   = b[0] >> 4;
        device_id = ((b[0] & 0xf) << 16) | (b[1] << 8) | b[2];
        crc       = (b[4] << 8) | b[5];
        event     = b[3];
    }

    if (device_id == 0 && crc == 0) {
        decoder_logf(decoder, 0, __func__, "DECODE_ABORT_EARLY id %d, crc %d", device_id, crc);
        return DECODE_ABORT_EARLY; // Reduce collisions
    }

    if (len > 50) { // DW11
        decoder_log_bitrow(decoder, 0, __func__, b, (len > 80 ? 80 : len), "");
    }

    if (channel == 0x2 || channel == 0x4 || channel == 0xA) {
        // 2GIG brand
        crc_calculated = crc16(b, 4, 0x8050, 0);
    }
    else { // channel == 0x8
        crc_calculated = crc16(b, 4, 0x8005, 0);
    }
    if (crc != crc_calculated) {
        decoder_logf(decoder, 0, __func__, "DECODE_FAIL_MIC crc %d, crc_calculated %d", crc, crc_calculated);
        //        return DECODE_FAIL_MIC; // Not a valid packet
    }

    // decoded event bits: CTRABHUU
    // NOTE: not sure if these apply to all device types
    contact     = (event & 0x80) >> 7;
    tamper      = (event & 0x40) >> 6;
    reed        = (event & 0x20) >> 5;
    alarm       = (event & 0x10) >> 4;
    battery_low = (event & 0x08) >> 3;
    heartbeat   = (event & 0x04) >> 2;

    /* clang-format off */
    data = data_make(
            "model",        "",         DATA_STRING, "Honeywell-Security",
            "id",           "",         DATA_FORMAT, "%05x", DATA_INT, device_id,
            "channel",      "",         DATA_INT,    channel,
            "event",        "",         DATA_FORMAT, "%02x", DATA_INT, event,
            "state",        "",         DATA_STRING, contact ? "open" : "closed", // Ignore the reed switch legacy.
            "contact_open", "",         DATA_INT,    contact,
            "reed_open",    "",         DATA_INT,    reed,
            "alarm",        "",         DATA_INT,    alarm,
            "tamper",       "",         DATA_INT,    tamper,
            "battery_ok",   "Battery",  DATA_INT,    !battery_low,
            "heartbeat",    "",         DATA_INT,    heartbeat,
            "mic",          "Integrity",    DATA_STRING, "CRC",
            "count",        "",         DATA_INT,    count,
            NULL);
    /* clang-format on */

    decoder_logf(decoder, 0, __func__, "honeywell_decode - end");
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "event",
        "state",
        "contact_open",
        "reed_open",
        "alarm",
        "tamper",
        "battery_ok",
        "heartbeat",
        "mic",
        "count",
        NULL,
};

r_device const honeywell = {
        .name        = "Honeywell Door/Window Sensor, 2Gig DW10/DW11, RE208 repeater",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 156,
        .long_width  = 0,
        .reset_limit = 292,
        .decode_fn   = &honeywell_decode,
        .fields      = output_fields,
};
