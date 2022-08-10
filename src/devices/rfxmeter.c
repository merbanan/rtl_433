/** @file
    RFXMeter decoder.

    Copyright (C) 2022 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
RFXMeter decoder.

S.a. https://github.com/merbanan/rtl_433/issues/2141

RFXMeter is using X10RF-like frame with some variations.
The data has the same meaning and order as through RFXCom (RFXtrx).

The device uses PPM encoding,
- 0 is encoded as 1 ms pulse and 1 ms gap,
- 1 is encoded as 1 ms pulse and 3 ms gap.
A packet starts with a 18 ms pulse, followed by a 8 ms gap us pulse.

Packet is 165 ms long, message is 139 ms long.
Packet is repeated 4 times for each address.

There is 70 ms between each packet.
Message ends with a 0 (10 then). True? (Or is it 1000 => 141 and 167 ms long and then 68 ms between packets).

A message is 48-byte long.

    AA AA CC CC CC TP

    |     Byte 0    |     Byte 1    |     Byte 2    |     Byte 3    |     Byte 4    |     Byte 5    |
     7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0
     4               4                   3                   2                   1
     7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
     < - - - - - - address - - - - > < - - - - - - - - counter value - - - - - - - > < - - > <parity>
                                     1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 2 2 2 2 1 1 1 1    |
                                     5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 3 2 1 0 9 8 7 6    packettype

2 bytes address. Byte 2 = byte 1 with the complement (bit 7-4). In this way, a maximum of 256 modules can be used.
The RFXPwr module counter is capable of measuring with an accuracy of 0.001kWh and can count from 0 to 16777.215 kWh.

Type of packets:

- 0000 normal data packet
- 0001 setting a new time interval.
   Byte 2
            0x01 30 seconds 0x02 1 minute
            0x04 6 minutes (RFXPower = 5 minutes)
            0x08 12 minutes (RFXPower = 10 minutes) 0x10 15 minutes
            0x20 30 minutes
            0x40 45 minutes
            0x80 60 minutes
- 0010 calibration value in <counter value> in μsec.
- 0011 setting a new address
- 0100 counter value reset to 0 1011 enter counter value
- 1100 enter interval setting mode within 5 seconds
- 1101 enter calibration mode within 5 seconds
- 1110 enter address setting mode within 5 seconds
- 1111 identification packet
      Byte 2 = firmware version
              0x00 - 0x3F = RFXPower
              0x40 - 0x7F = RFU
              0x80 - 0xBF = RFU
              0xC0 - 0xFF = RFXMeter
      Byte 3 = time interval (see packet type 0001)

The 4 bits for parity are computed as nibble sum over the whole message.

*/

#include "decoder.h"

static int rfxmeter_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // All RfxMeter packets have one row.
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_LENGTH;
    }

    // All RfxMeter packets have 48 bits.
    if (bitbuffer->bits_per_row[0] != 48) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[0];

    // Check address complement
    if ((b[0] ^ b[1]) != 0) {
        return DECODE_FAIL_SANITY;
    }

    // Check message parity
    int chk = add_nibbles(b, 6);
    if (chk != 0) {
        return DECODE_FAIL_MIC;
    }

    int address   = b[0];
    // int address_c = b[1];
    // int msb       = b[4];
    // int isb       = b[2];
    // int lsb       = b[3];
    int msg_value = (b[4] << 16) | (b[2] << 8) | (b[3]);
    int msg_type  = b[5] >> 4;
    // int parity    = b[5] & 0x0f;

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "RfxMeter",
            "id",           "Id",           DATA_INT,    address,
            "msg_type",     "Msg Type",     DATA_INT,    msg_type,
            "msg_value",    "Msg Value",    DATA_INT,    msg_value,
            "mic",          "Integrity",    DATA_STRING, "PARITY",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "msg_type",
        "msg_value",
        "mic",
        NULL,
};

r_device rfxmeter = {
        .name        = "RfxMeter, RFXPwr",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 3000,
        .reset_limit = 4000,
        .decode_fn   = &rfxmeter_decode,
        .fields      = output_fields,
};
