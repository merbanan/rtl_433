/** @file
    Microchip HCS200 KeeLoq Code Hopping Encoder based remotes.

    Copyright (C) 2019, 667bdrm

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Microchip HCS200 KeeLoq Code Hopping Encoder based remotes.

66 bits transmitted, LSB first

|  0-31 | Encrypted Portion
| 32-59 | Serial Number
| 60-63 | Button Status
|  64   | Battery Low
|  65   | Fixed 1

Datasheet: DS40138C http://ww1.microchip.com/downloads/en/DeviceDoc/40138c.pdf

The warmup of 12 short pulses is followed by a long 4400 us gap.
There are two packets with a 17500 us gap.

rtl_433 -R 0 -X 'n=hcs200,m=OOK_PWM,s=370,l=772,r=9000,g=1500,t=152'
*/

#include "decoder.h"

static int hcs200_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[0];
    uint32_t encrypted, serial, encrypted_rev, serial_rev;
    char encrypted_str[9];
    char encrypted_rev_str[9];
    char serial_str[9];
    char serial_rev_str[9];

    // Reject codes of wrong length
    if (bitbuffer->bits_per_row[0] != 12 || bitbuffer->bits_per_row[1] != 66)
        return DECODE_ABORT_LENGTH;

    // Reject codes with an incorrect preamble (expected 0xfff)
    if (b[0] != 0xff || (b[1] & 0xf0) != 0xf0) {
        if (decoder->verbose > 1)
            fprintf(stderr, "%s: Preamble not found\n", __func__);
        return DECODE_ABORT_EARLY;
    }

    // Second row is data
    b = bitbuffer->bb[1];

    // No need to decode/extract values for simple test
    if (b[1] == 0xff && b[2] == 0xff && b[3] == 0xff && b[4] == 0xff
            && b[5] == 0xff && b[6] == 0xff && b[7] == 0xff) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: DECODE_FAIL_SANITY data all 0xff\n", __func__);
        }
        return DECODE_FAIL_SANITY;
    }

    encrypted     = ((unsigned)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3]);
    serial        = (b[4] << 20) | (b[5] << 12) | (b[6] << 4) | (b[7] >> 4);
    encrypted_rev = reverse32(encrypted);
    serial_rev    = reverse32(serial);

    sprintf(encrypted_str, "%08X", encrypted);
    sprintf(serial_str, "%08X", serial);
    sprintf(encrypted_rev_str, "%08X", encrypted_rev);
    sprintf(serial_rev_str, "%08X", serial_rev);

    /* clang-format off */
    data = data_make(
            "model",            "",         DATA_STRING,    "Microchip-HCS200",
            "id",               "",         DATA_STRING,    serial_str,
            "id_rev",           "",         DATA_STRING,    serial_rev_str,
            "encrypted",        "",         DATA_STRING,    encrypted_str,
            "encrypted_rev",    "",         DATA_STRING,    encrypted_rev_str,
            "button1",          "",         DATA_STRING,    ((b[7] & 0x04) == 0x04) ? "ON" : "OFF",
            "button2",          "",         DATA_STRING,    ((b[7] & 0x02) == 0x02) ? "ON" : "OFF",
            "button3",          "",         DATA_STRING,    ((b[7] & 0x09) == 0x09) ? "ON" : "OFF",
            "button4",          "",         DATA_STRING,    ((b[7] & 0x06) == 0x06) ? "ON" : "OFF",
            "misc",             "",         DATA_STRING,    (b[7] == 0x0F) ? "ALL_PRESSED" : "",
            "battery_ok",       "Battery",  DATA_INT,       (((b[8] >> 4) & 0x08) == 0x08) ? 0 : 1,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "id_rev",
        "encrypted",
        "encrypted_rev",
        "button1",
        "button2",
        "button3",
        "button4",
        "misc",
        "battery_ok",
        NULL,
};

r_device hcs200 = {
        .name        = "Microchip HCS200 KeeLoq Hopping Encoder based remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 370,
        .long_width  = 772,
        .gap_limit   = 1500,
        .reset_limit = 9000,
        .tolerance   = 152, // us
        .decode_fn   = &hcs200_callback,
        .fields      = output_fields,
};
