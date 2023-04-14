/** @file
    Akhan remote keyless entry system.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Akhan remote keyless entry system.

This RKE system uses a HS1527 OTP encoder (http://sc-tech.cn/en/hs1527.pdf)
Each message consists of a preamble, 20 bit id and 4 data bits.

(code based on chuango.c and generic_remote.c)

Note: simple 24 bit fixed ID protocol (x1527 style) and should be handled by the flex decoder.
*/

#include "decoder.h"

static int akhan_rke_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int id;
    int cmd;
    char const *cmd_str;

    if (bitbuffer->bits_per_row[0] != 25)
        return DECODE_ABORT_LENGTH;
    b = bitbuffer->bb[0];

    // invert bits, short pulse is 0, long pulse is 1
    b[0] = ~b[0];
    b[1] = ~b[1];
    b[2] = ~b[2];

    id  = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);
    cmd = b[2] & 0x0F;
    switch (cmd) {
    case 0x1: cmd_str = "0x1 (Lock)"; break;
    case 0x2: cmd_str = "0x2 (Unlock)"; break;
    case 0x4: cmd_str = "0x4 (Mute)"; break;
    case 0x8: cmd_str = "0x8 (Alarm)"; break;
    default: cmd_str = NULL; break;
    }

    if (!cmd_str)
        return DECODE_FAIL_SANITY;

    /* clang-format off */
    data = data_make(
            "model",    "",             DATA_STRING, "Akhan-100F14",
            "id",       "ID (20bit)",   DATA_FORMAT, "0x%x", DATA_INT, id,
            "data",     "Data (4bit)",  DATA_STRING, cmd_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "data",
        NULL,
};

r_device const akhan_100F14 = {
        .name        = "Akhan 100F14 remote keyless entry",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 316,
        .long_width  = 1020,
        .reset_limit = 1800,
        .sync_width  = 0,
        .tolerance   = 80, // us
        .decode_fn   = &akhan_rke_callback,
        .fields      = output_fields,
};
