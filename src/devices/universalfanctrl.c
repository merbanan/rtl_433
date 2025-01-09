/** @file
    Decoder for 'Universal reversable Fan controller 24V'

    Copyright (C) 2025 Marcel Verpaalen <marcel@verpaalen.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Decoder for 'Universal (Reverseable) 24V Fan Controller'.

The device uses PWM encoding,
- 0 is encoded as 756 us pulse and 252 us gap,
- 1 is encoded as 256 us pulse and 756 us gap.

A transmission starts with a pulse of 3616 us,
there a 7 repeated packets, each with a 8200 us gap.

Data layout:
    AAAAAAAAAAAAAAAAAAAABBBBBCCCC1

- A: 20 bit Address / id
- B: 5-bit buttoncode
- C: 4 bit Checksum, init 0x0A
- 1: Always 1

Example:
    (without CRC check using flex decode)
     $ rtl_433  -X 'n=fan,m=OOK_PWM,s=252,l=756,r=8288,g=0,t=0,y=3620,bits=33,get=address:@0:{20};%x,get=key:@20:{5}:[0x19:All_off 0x17:Light 0x1b:Forward 0xa:Fan 0xe:Reverse 9:Fan_off 0xf:Speed1 0xd:Speed2 3:Speed3 0x15:Speed4 0x10:Speed5 0x13:speed6 0x1d:1H 0x16:2H 6:3H ],get=seq:@25:{3}:%d,get=crc:@28:{4}:%02x,unique' -F kv
*/

#include "decoder.h"

#define UNIVERSALFAN__BITLEN     33
#define UNIVERSALFAN__MINREPEATS 3

static int universalfan_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    for (uint16_t i = 0; i < bitbuffer->num_rows; i++) {
        uint16_t l = bitbuffer->bits_per_row[i];
        decoder_logf(decoder, 1, __func__, "bits_per_row in row [%d] (total rows: %d %d) = %d", i, bitbuffer->num_rows, bitbuffer->num_rows, l);
        if (l >= 33) {
            uint8_t *b    = bitbuffer->bb[i];
            uint8_t cksum = 0xA;
            for (int j = 0; j < 28; j += 4) {
                cksum ^= (b[j / 8] >> (4 * (1 - (j % 8) / 4))) & 0x0F;
            }
            int crc_msg = (b[3] & 0x0F);
            decoder_logf(decoder, 1, __func__, "CRC calculated: %d CRC message: %d", cksum, crc_msg);
        }
    }

    int row = bitbuffer_find_repeated_row(bitbuffer, UNIVERSALFAN__MINREPEATS, UNIVERSALFAN__BITLEN);
    if (row < 0)
        return DECODE_ABORT_LENGTH;

    uint8_t *b = bitbuffer->bb[row];
    int crc    = 0xA;
    for (int j = 0; j < 28; j += 4) {
        crc ^= (b[j / 8] >> (4 * (1 - (j % 8) / 4))) & 0x0F;
    }

    int crc_msg = (b[3] & 0x0F);
    decoder_logf(decoder, 1, __func__, "CRC calculated: %d CRC message: %d", crc, crc_msg);

    if (crc_msg != crc) {
        decoder_logf(decoder, 1, __func__, "CRC calculated: %d CRC message: %d", crc, crc_msg);
        return DECODE_FAIL_MIC;
    }

    int address = (b[0] << 12) + (b[1] << 4) + (b[2] >> 4);    // @0 {20};
    int button  = ((b[2] & 0x0F) << 1) + ((b[3] & 0x80) >> 7); // @20 {5}
    int counter = (b[3] & 0x7F) >> 4;                          // @25 {3}
    // int rc      = (b[3] & 0x0F);                               // @28 {4}
    char const *button_string;

    switch (button) {
    case 0x19:
        button_string = "All Off";
        break;
    case 0x17:
        button_string = "Light On/Off";
        break;
    case 0x1b:
        button_string = "Forward";
        break;
    case 0xa:
        button_string = "Fan";
        break;
    case 0xe:
        button_string = "Reverse";
        break;
    case 0x09:
        button_string = "Fan Off";
        break;
    case 0xf:
        button_string = "Speed 1";
        break;
    case 0xd:
        button_string = "Speed 2";
        break;
    case 0x03:
        button_string = "Speed 3";
        break;
    case 0x15:
        button_string = "Speed 4";
        break;
    case 0x10:
        button_string = "Speed 5";
        break;
    case 0x13:
        button_string = "speed 6";
        break;
    case 0x1d:
        button_string = "1H";
        break;
    case 0x16:
        button_string = "2H";
        break;
    case 0x06:
        button_string = "3H";
        break;
    default:
        button_string = "Unknown";
        break;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",                 DATA_STRING, "Universal-Fan-remote",
            "address",      "Transmitter ID",   DATA_INT,    address,
            "button",       "Button",           DATA_STRING, button_string , 
            "button_code",  "Button Code",      DATA_INT,    button,
            "counter",      "Rolling Counter",  DATA_INT,    counter,
            "crc",          "CRC" ,             DATA_INT,    crc_msg,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "address",
        "button",
        "button_code",
        "counter",
        "crc",
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
