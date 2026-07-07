/** @file
    Decoder for 'Universal reversable Fan controller 24V'.

    Copyright (C) 2025 Marcel Verpaalen <marcel@verpaalen.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/

#include "decoder.h"

/**
Decoder for 'Universal (Reverseable) 24V Fan Controller'.

The device uses PWM encoding,
- 0 is encoded as 756 us pulse and 252 us gap,
- 1 is encoded as 256 us pulse and 756 us gap.

A transmission starts with a pulse of 3616 us,
there a 7 repeated packets, each with a 8200 us gap.

Data layout:
    AAAAAAAAAAAAAAAAAAAABBBBBRRRRCCCC1

- A: 20 bit Address / id
- B: 5-bit buttoncode
- R: 3 bit rolling counter
- C: 4 bit Checksum, init 0x0A
- 1: Always 1

Example:
    (without checksum check using flex decode)
     $ rtl_433 -X 'n=fan,m=OOK_PWM,s=252,l=756,r=8288,g=0,t=0,y=3620,bits=33,
        get=address:@0:{20};%x,get=address:@20:{5},get=msgcount:@25:{3}:%d,get=crc:@28:{4}:%02x,unique'
*/

static int universalfan_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 3, 33);
    if (row < 0) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b  = bitbuffer->bb[row];
    // int chk_msg = (b[3] & 0x0F);
    int sum = xor_bytes(b, 4); // xor message bytes, last byte also has the checksum
    sum     = (sum >> 4) ^ (sum & 0xf); // fold nibbles
    if (sum != 0xa) {
        decoder_log(decoder, 1, __func__, "Checksum error.");
        return DECODE_FAIL_MIC;
    }

    int address = (b[0] << 12) + (b[1] << 4) + (b[2] >> 4);    // @0 {20};
    int button  = ((b[2] & 0x0F) << 1) + ((b[3] & 0x80) >> 7); // @20 {5}
    int counter = (b[3] & 0x7F) >> 4;                          // @25 {3}
    // int rc      = (b[3] & 0x0F);                               // @28 {4}
    char const *button_str;

    switch (button) {
    case 0x19:
        button_str = "All Off";
        break;
    case 0x17:
        button_str = "Light On/Off";
        break;
    case 0x1b:
        button_str = "Forward";
        break;
    case 0x0a:
        button_str = "Fan";
        break;
    case 0x0e:
        button_str = "Reverse";
        break;
    case 0x09:
        button_str = "Fan Off";
        break;
    case 0x0f:
        button_str = "Speed 1";
        break;
    case 0x0d:
        button_str = "Speed 2";
        break;
    case 0x03:
        button_str = "Speed 3";
        break;
    case 0x15:
        button_str = "Speed 4";
        break;
    case 0x10:
        button_str = "Speed 5";
        break;
    case 0x13:
        button_str = "speed 6";
        break;
    case 0x1d:
        button_str = "1H";
        break;
    case 0x16:
        button_str = "2H";
        break;
    case 0x06:
        button_str = "3H";
        break;
    default:
        button_str = "Unknown";
        break;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",                 DATA_STRING, "UniFan-24V",
            "id",           "Transmitter ID",   DATA_INT,    address,
            "button",       "Button",           DATA_STRING, button_str,
            "button_code",  "Button Code",      DATA_INT,    button,
            "counter",      "Rolling Counter",  DATA_INT,    counter,
            "mic",          "",                 DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        "button_code",
        "counter",
        "mic",
        NULL,
};

r_device const universalfanctrl = {
        .name        = "Universal (Reverseable) 24V Fan Controller",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 256,
        .long_width  = 756,
        .gap_limit   = 8000,
        .sync_width  = 3616,
        .reset_limit = 8800,
        .decode_fn   = &universalfan_decode,
        .fields      = output_fields,
};
