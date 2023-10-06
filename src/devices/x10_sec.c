/** @file
    X10 Security sensor decoder.

    Copyright (C) 2018 Anthony Kava

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
X10 Security sensor decoder.

Each packet starts with a sync pulse of 9000 us and 4500 us gap.
The message is OOK PPM encoded with 562 us pulse and long gap (0 bit)
of 1687 us or short gap (1 bit) of 562 us. There are 41 bits, the
message is repeated 5 times with a packet gap of 40000 us.

The protocol has a lot of similarities to the NEC IR protocol

Bits 0-7 are first part of the device ID
Bits 8-11 should be identical to bits 0-3
Bits 12-15 should be the XOR function of bits 4-7
Bits 16-23 are the code/message sent
Bits 24-31 should be the XOR function of bits 16-23
Bits 32-39 are the second part of the device ID
Bit 40 is CRC checksum (even parity)

Tested with American sensors operating at 310 MHz
e.g., rtl_433 -f 310.558M

Tested with European/International sensors, DS18, KR18 and MS18 operating at 433 MHz
e.g., rtl_433

American sensor names ends with an 'A', like DS18A, while European/International
sensor names ends with an 'E', like MS18E

The byte value decoding is based on limited observations, and it is likely
that there are missing pieces.

DS10 & DS18 door/window sensor bitmask : CTUUUDUB
     C = Door/window closed flag.
     T = Tamper alarm. Set to 1 if lid is open. (Not supported on DS10)
     U = Unknown. Cleared in all samples.
     D = Delay setting. Min=1. Max=0.
     B = Battery low flag.

DS18 has both a magnetic (reed) relay and an external input. The two inputs
are reported using two different ID's as if they were two separate sensors.

MS10 does not support tamper alarm, while MS18 does

Based on code provided by Willi 'wherzig' in issue #30 (2014-04-21)

*/

#include "decoder.h"

static int x10_sec_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;                       /* bits of a row            */
    char const *event_str = "UNKNOWN"; /* human-readable event     */
    int battery_low       = 0;         /* battery indicator        */
    int delay             = 0;         /* delay setting            */
    uint8_t tamper        = 0;         /* tamper alarm indicator   */
    uint8_t parity        = 0;         /* for CRC calculation      */

    if (bitbuffer->num_rows != 2)
        return DECODE_ABORT_EARLY;

    /* First row should be sync, second row should be 41 bit message */
    if (bitbuffer->bits_per_row[1] < 41) {
        if (bitbuffer->bits_per_row[1] != 0)
            decoder_logf(decoder, 1, __func__, "DECODE_ABORT_LENGTH, Received message length=%i", bitbuffer->bits_per_row[1]);
        return DECODE_ABORT_LENGTH;
    }

    b = bitbuffer->bb[1];

    /* validate what we received */
    if ((b[0] ^ b[1]) != 0x0f || (b[2] ^ b[3]) != 0xff) {
        decoder_logf(decoder, 1, __func__, "DECODE_FAIL_SANITY, b0=%02x b1=%02x b2=%02x b3=%02x", b[0], b[1], b[2], b[3]);
        return DECODE_FAIL_SANITY;
    }

    /* Check CRC */
    parity = b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4] ^ (b[5] & 0x80); // parity as byte
    parity = (parity >> 4) ^ (parity & 0xf);                   // fold to nibble
    parity = (parity >> 2) ^ (parity & 0x3);                   // fold to 2 bits
    parity = (parity >> 1) ^ (parity & 0x1);                   // fold to 1 bit
    if (parity) {                                              // even parity - parity should be zero
        decoder_logf(decoder, 1, __func__, "DECODE_FAIL_MIC CRC Fail, b0=%02x b1=%02x b2=%02x b3=%02x b4=%02x b5-CRC-bit=%02x", b[0], b[1], b[2], b[3], b[4], (b[5] & 0x80));
        return DECODE_FAIL_MIC;
    }

    /* We have received a valid message, decode it */

    battery_low = b[2] & 0x01;

    /* set event_str based on code received */
    switch (b[2] & 0xfe) {
    case 0x00: // OPEN
    case 0x04: // OPEN & Delay
    case 0x40: // OPEN & Tamper Alarm
    case 0x44: // OPEN & Tamper Alarm & Delay
        event_str = "DOOR/WINDOW OPEN";
        delay     = !(b[2] & 0x04);
        tamper    = (b[2] & 0x40) >> 6;
        break;
    case 0x80: // CLOSED
    case 0x84: // CLOSED & Delay
    case 0xc0: // CLOSED & Tamper Alarm
    case 0xc4: // CLOSED & Tamper Alarm & Delay
        event_str = "DOOR/WINDOW CLOSED";
        delay     = !(b[2] & 0x04);
        tamper    = (b[2] & 0x40) >> 6;
        break;
    case 0x06:
        event_str = "KEY-FOB ARM";
        break;
    case 0x0c: // MOTION TRIPPED
    case 0x4c: // MOTION TRIPPED & Tamper Alarm
        event_str = "MOTION TRIPPED";
        tamper    = (b[2] & 0x40) >> 6;
        break;
    case 0x26:
        event_str = "KR18 PANIC";
        break;
    case 0x42:
        event_str = "KEY-FOB LIGHTS A ON"; // KR18
        break;
    case 0x46:
        event_str = "KEY-FOB LIGHTS B ON"; // KR15 and KR18
        break;
    case 0x82:
        event_str = "SH624 SEC-REMOTE DISARM";
        break;
    case 0x86:
        event_str = "KEY-FOB DISARM";
        break;
    case 0x88:
        event_str = "KR15 PANIC";
        break;
    case 0x8c: // MOTION READY
    case 0xcc: // MOTION READY & Tamper Alarm
        event_str = "MOTION READY";
        tamper    = (b[2] & 0x40) >> 6;
        break;
    case 0x98:
        event_str = "KR15 PANIC-3SECOND";
        break;
    case 0xc2:
        event_str = "KEY-FOB LIGHTS A OFF"; // KR18
        break;
    case 0xc6:
        event_str = "KEY-FOB LIGHTS B OFF"; // KR15 and KR18
        break;
    }

    /* get x10_id_str, x10_code_str ready for output */
    char x10_id_str[12];
    snprintf(x10_id_str, sizeof(x10_id_str), "%02x%02x", b[0], b[4]);
    char x10_code_str[5];
    snprintf(x10_code_str, sizeof(x10_code_str), "%02x", b[2]);

    /* debug output */
    decoder_logf_bitbuffer(decoder, 1, __func__, bitbuffer, "id=%02x%02x code=%02x event_str=%s", b[0], b[4], b[2], event_str);

    /* build and handle data set for normal output */
    /* clang-format off */
    data = data_make(
            "model",        "",             DATA_STRING, "X10-Security",
            "id",           "Device ID",    DATA_STRING, x10_id_str,
            "code",         "Code",         DATA_STRING, x10_code_str,
            "event",        "Event",        DATA_STRING, event_str,
            "delay",        "Delay",        DATA_COND,   delay,         DATA_INT, delay,
            "battery_ok",   "Battery",      DATA_COND,   battery_low,   DATA_INT, !battery_low,
            "tamper",       "Tamper",       DATA_COND,   tamper,        DATA_INT, tamper,
            "mic",          "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "code",
        "event",
        "delay",
        "battery_ok",
        "tamper",
        "mic",
        NULL,
};

/* r_device definition */
r_device const x10_sec = {
        .name        = "X10 Security",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 562,  // Short gap 562us
        .long_width  = 1687, // Long gap 1687us
        .gap_limit   = 2200, // Gap after sync is 4.5ms (1125)
        .reset_limit = 6000,
        .decode_fn   = &x10_sec_callback,
        .fields      = output_fields,
};
