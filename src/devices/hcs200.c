/** @file
    Microchip HCS200/HCS300 KeeLoq Code Hopping Encoder based remotes.

    Copyright (C) 2019, 667bdrm

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Microchip HCS200/HCS300 KeeLoq Code Hopping Encoder based remotes.

66 bits transmitted, LSB first.

|  0-31 | Encrypted Portion
| 32-59 | Serial Number
| 60-63 | Button Status (S3, S0, S1, S2)
|  64   | Battery Low
|  65   | Fixed 1

Note that the button bits are (MSB/first sent to LSB) S3, S0, S1, S2.
Hardware buttons might map to combinations of these bits.

Some remotes send the hopping code unencrypted, with B the button bits in transmit
order (S2, S1, S0, S3 from bit 0):

- HCS101: hopping code is CCCC B 000, C a 16 bit counter
- AN-Motors: hopping code is CC CC B 404, C an 8 bit counter sent twice

These are reported with a subtype and the counter. The subtype is matched on the hopping code
pattern only: a normal encrypted hopping code gets the HCS101 subtype and a meaningless counter
with a chance of about 1 in 65536 (AN-Motors about 1 in 16 million). Each message is a package of
its own (17.5 ms gap), so the pattern can't be checked on a repeat.

In programming mode (all button bits set) some remotes send a fixed hopping code:

- Aprimatic: hopping code is 1A2B3C4D
- DEA Mio: hopping code is 00000000

These are reported with a subtype only.

- Datasheet HCS200: http://ww1.microchip.com/downloads/en/devicedoc/40138c.pdf
- Datasheet HCS300: http://ww1.microchip.com/downloads/en/devicedoc/21137g.pdf

The warm-up of 12 short pulses is followed by a long 4400 us gap.
There are two packets with a 17500 us gap.

rtl_433 -R 0 -X 'n=hcs200,m=OOK_PWM,s=370,l=772,r=9000,g=1500,t=152'
*/

#include "decoder.h"

static int hcs200_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Reject codes of wrong length
    if (bitbuffer->bits_per_row[0] != 12 || bitbuffer->bits_per_row[1] != 66)
        return DECODE_ABORT_LENGTH;

    uint8_t *b = bitbuffer->bb[0];
    // Reject codes with an incorrect preamble (expected 0xfff)
    if (b[0] != 0xff || (b[1] & 0xf0) != 0xf0) {
        decoder_log(decoder, 2, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }

    // Second row is data
    b = bitbuffer->bb[1];

    // No need to decode/extract values for simple test
    if (b[1] == 0xff && b[2] == 0xff && b[3] == 0xff && b[4] == 0xff
            && b[5] == 0xff && b[6] == 0xff && b[7] == 0xff) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0xff");
        return DECODE_FAIL_SANITY;
    }

    // The transmission is LSB first, big endian.
    uint32_t encrypted = ((unsigned)reverse8(b[3]) << 24) | (reverse8(b[2]) << 16) | (reverse8(b[1]) << 8) | (reverse8(b[0]));
    int serial         = (reverse8(b[7] & 0xf0) << 24) | (reverse8(b[6]) << 16) | (reverse8(b[5]) << 8) | (reverse8(b[4]));
    int btn            = (b[7] & 0x0f);
    int btn_num        = (btn & 0x08) | ((btn & 0x01) << 2) | (btn & 0x02) | ((btn & 0x04) >> 2); // S3, S0, S1, S2
    int learn          = (b[7] & 0x0f) == 0x0f;
    int battery_low    = (b[8] & 0x80) == 0x80;
    int repeat         = (b[8] & 0x40) == 0x40;

    // Unencrypted hopping codes, the button bits in transmit order
    int btn_tx          = reverse8(btn) >> 4;
    char const *subtype = "";
    int counter         = 0;
    int has_counter     = 0;
    if (learn && encrypted == 0x1a2b3c4d) {
        subtype = "Aprimatic";
    }
    else if (learn && encrypted == 0) {
        subtype = "DEA-Mio";
    }
    else if ((encrypted & 0xfff) == 0x404 && (encrypted >> 24) == ((encrypted >> 16) & 0xff) && ((encrypted >> 12) & 0xf) == (unsigned)btn_tx) {
        subtype     = "AN-Motors";
        counter     = encrypted >> 24;
        has_counter = 1;
    }
    else if ((encrypted & 0xfff) == 0 && ((encrypted >> 12) & 0xf) == (unsigned)btn_tx) {
        subtype     = "HCS101";
        counter     = encrypted >> 16;
        has_counter = 1;
    }

    char encrypted_str[9];
    snprintf(encrypted_str, sizeof(encrypted_str), "%08X", encrypted);
    char serial_str[9];
    snprintf(serial_str, sizeof(serial_str), "%07X", serial);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING,    "Microchip-HCS200",
            "id",               "",             DATA_STRING,    serial_str,
            "battery_ok",       "Battery",      DATA_INT,       !battery_low,
            "button",           "Button",       DATA_INT,       btn_num,
            "learn",            "Learn mode",   DATA_INT,       learn,
            "repeat",           "Repeat",       DATA_INT,       repeat,
            "encrypted",        "",             DATA_STRING,    encrypted_str,
            "subtype",          "Type",         DATA_COND,      *subtype,        DATA_STRING, subtype,
            "counter",          "Counter",      DATA_COND,      has_counter,     DATA_INT,    counter,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "button",
        "learn",
        "repeat",
        "encrypted",
        "subtype",
        "counter",
        NULL,
};

r_device const hcs200 = {
        .name        = "Microchip HCS200/HCS300 KeeLoq Hopping Encoder based remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 370,
        .long_width  = 772,
        .gap_limit   = 1500,
        .reset_limit = 9000,
        .tolerance   = 152,
        .decode_fn   = &hcs200_callback,
        .fields      = output_fields,
};

r_device const hcs200_fsk = {
        .name        = "Microchip HCS200/HCS300 KeeLoq Hopping Encoder based remotes (FSK)",
        .modulation  = FSK_PULSE_PWM,
        .short_width = 370,
        .long_width  = 772,
        .gap_limit   = 1500,
        .reset_limit = 9000,
        .tolerance   = 152,
        .decode_fn   = &hcs200_callback,
        .fields      = output_fields,
};
