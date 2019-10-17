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

rtl_433 -R 0 -X 'n=name,m=OOK_PWM,s=370,l=772,r=14000,g=4000,t=152,y=0,preamble={12}0xfff'
*/

#include "decoder.h"

static int hcs200_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[0];
    int i;
    uint32_t encrypted, serial;
    char encrypted_str[9];
    char serial_str[9];

    /* Reject codes of wrong length */
    if (78 != bitbuffer->bits_per_row[0])
        return DECODE_ABORT_LENGTH;

    /* Reject codes with an incorrect preamble (expected 0xfff) */
    if (b[0] != 0xff || (b[1] & 0xf0) != 0xf0) {
        if (decoder->verbose > 1)
            fprintf(stderr, "HCS200: Preamble not found\n");
        return DECODE_ABORT_EARLY;
    }

    // align buffer, shifting by 4 bits
    for (i = 1; i < 10; i++) {
        b[i] = (b[i] << 4) | (b[i + 1] >> 4);
    }

    encrypted = ((unsigned)b[1] << 24) | (b[2] << 16) | (b[3] << 8) | (b[4]);
    serial    = (b[5] << 20) | (b[6] << 12) | (b[7] << 4) | (b[8] >> 4);

    sprintf(encrypted_str, "%08X", encrypted);
    sprintf(serial_str, "%08X", serial);

    /* clang-format off */
    data = data_make(
            "model",        "", DATA_STRING,    "Microchip-HCS200",
            "id",           "", DATA_STRING,    serial_str,
            "encrypted",    "", DATA_STRING,    encrypted_str,
            "button1",      "", DATA_STRING,    ((b[8] & 0x04) == 0x04) ? "ON" : "OFF",
            "button2",      "", DATA_STRING,    ((b[8] & 0x02) == 0x02) ? "ON" : "OFF",
            "button3",      "", DATA_STRING,    ((b[8] & 0x09) == 0x09) ? "ON" : "OFF",
            "button4",      "", DATA_STRING,    ((b[8] & 0x06) == 0x06) ? "ON" : "OFF",
            "misc",         "", DATA_STRING,    (b[8] == 0x0F) ? "ALL_PRESSED" : "",
            "battery_ok",   "", DATA_INT,       (((b[9] >> 4) & 0x08) == 0x08) ? 0 : 1,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "encrypted",
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
        .gap_limit   = 4000,
        .reset_limit = 14000,
        .sync_width  = 0,   // No sync bit used
        .tolerance   = 152, // us
        .decode_fn   = &hcs200_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
