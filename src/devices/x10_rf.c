/** @file
    X10 sensor (Non-security devices).

    Copyright (C) 2015 Tommy Vestermark
    Mods. by Dave Fleck 2021

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
X10  sensor decoder.

Each packet starts with a sync pulse of 9000 us (16x a bit time)
and a 4500 us gap.
The message is OOK PPM encoded with 562.5 us pulse and long gap (0 bit)
of 1687.5 us or short gap (1 bit) of 562.5 us.

There are 32bits. The message is repeated 5 times with
a packet gap of 40000 us.

The protocol has a lot of similarities to the NEC IR protocol

The second byte is the inverse of the first.
The fourth byte is the inverse of the third.

Based on protocol informtation found at:
http://www.wgldesigns.com/protocols/w800rf32_protocol.txt

Tested with American sensors operating at 310 MHz
e.g., rtl_433 -f 310M -R 22

Seems to work best with 2 MHz sample rate:
rtl_433 -f 310M -R 22 -s 2M

Tested with HR12A, RMS18, HD23A, MS14A, PMS03, MS12A,
RMS18, Radio Shack 61-2675-T

*/

#include "decoder.h"

static int x10_rf_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[1];

    uint8_t arrbKnownConstBitMask[4]  = {0x0B, 0x0B, 0x07, 0x07};
    uint8_t arrbKnownConstBitValue[4] = {0x00, 0x0B, 0x00, 0x07};

    // Row [0] is sync pulse
    // Validate length
    if (bitbuffer->bits_per_row[1] != 32) { // Don't waste time on a wrong length package
        if (bitbuffer->bits_per_row[1] != 0)
            decoder_logf(decoder, 1, __func__, "DECODE_ABORT_LENGTH, Received message length=%i", bitbuffer->bits_per_row[1]);
        return DECODE_ABORT_LENGTH;
    }

    // Validate complement values
    if ((b[0] ^ b[1]) != 0xff || (b[2] ^ b[3]) != 0xff) {
        decoder_logf(decoder, 1, __func__, "DECODE_FAIL_SANITY, b0=%02x b1=%02x b2=%02x b3=%02x", b[0], b[1], b[2], b[3]);
        return DECODE_FAIL_SANITY;
    }

    // Some bits are constant.
    for (int8_t bIdx = 0; bIdx < 4; bIdx++) {
        uint8_t bTest = arrbKnownConstBitMask[bIdx] & b[bIdx];  // Mask the appropriate bits

        if (bTest != arrbKnownConstBitValue[bIdx]) {  // If resulting bits are incorrectly set
            decoder_logf(decoder, 1, __func__, "DECODE_FAIL_SANITY, b0=%02x b1=%02x b2=%02x b3=%02x", b[0], b[1], b[2], b[3]);
            return DECODE_FAIL_SANITY;
        }
    }

    // We have received a valid message, decode it

    unsigned code = (unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];

    uint8_t bHouseCode  = 0;
    uint8_t bDeviceCode = 0;
    uint8_t arrbHouseBits[4] = {0, 0, 0, 0};

    // Extract House bits
    arrbHouseBits[0] = (b[0] & 0x80) >> 7;
    arrbHouseBits[1] = (b[0] & 0x40) >> 6;
    arrbHouseBits[2] = (b[0] & 0x20) >> 5;
    arrbHouseBits[3] = (b[0] & 0x10) >> 4;

    // Convert bits into integer
    bHouseCode   = (~(arrbHouseBits[0] ^ arrbHouseBits[1])  & 0x01) << 3;
    bHouseCode  |= ( ~arrbHouseBits[1]                      & 0x01) << 2;
    bHouseCode  |= ( (arrbHouseBits[1] ^ arrbHouseBits[2])  & 0x01) << 1;
    bHouseCode  |=    arrbHouseBits[3]                      & 0x01;

    // Extract and convert Unit bits to integer
    bDeviceCode  = (b[0] & 0x04) << 1;
    bDeviceCode |= (b[2] & 0x40) >> 4;
    bDeviceCode |= (b[2] & 0x08) >> 2;
    bDeviceCode |= (b[2] & 0x10) >> 4;
    bDeviceCode += 1;

    char housecode[2] = {0};
    *housecode = bHouseCode + 'A';

    int state = (b[2] & 0x20) == 0x00;

    char const *event_str = "UNKNOWN";         // human-readable event

    if ((b[2] & 0x80) == 0x80) {         // Special event bit
        bDeviceCode = 0;                 // No device for special events

        switch (b[2]) {
        case 0x98:
            event_str = "DIM";
            break;
        case 0x88:
            event_str = "BRI";
            break;
        case 0x90:
            event_str = "ALL LTS ON";
            break;
        case 0x80:
            event_str = "ALL OFF";
            break;
        }
    }
    else {
        event_str = state ? "ON" : "OFF";
    }

    // debug output
    decoder_logf_bitbuffer(decoder, 1, __func__, bitbuffer, "id=%s%i event_str=%s", housecode, bDeviceCode, event_str);

    /* clang-format off */
    data = data_make(
            "model",        "",             DATA_STRING, "X10-RF",
            "id",           "",             DATA_INT,    bDeviceCode,
            "channel",      "",             DATA_STRING, housecode,
            "state",        "State",        DATA_STRING, event_str,
            "data",         "Data",         DATA_FORMAT, "%08x", DATA_INT, code,
            "mic",          "Integrity",    DATA_STRING, "PARITY",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "channel",
        "id",
        "state",
        "data",
        "mic",
        NULL,
};

r_device const X10_RF = {
        .name        = "X10 RF",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 562,  // Short gap 562.5 µs
        .long_width  = 1687, // Long gap 1687.5 µs
        .gap_limit   = 2200, // Gap after sync is 4.5ms (1125)
        .reset_limit = 6000, // Gap seen between messages is ~40ms so let's get them individually
        .decode_fn   = &x10_rf_callback,
        .fields      = output_fields,
};
