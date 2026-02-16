/** @file
    Restaurant pager system (EV1527-variant, 25-bit OOK PWM).

    Copyright (C) 2026 Jeff Laflamme

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Restaurant pager system (EV1527-variant, 25-bit OOK PWM).

Tested with JianTao JT-913 restaurant guest paging system.
Commonly found at 315 MHz or 433.92 MHz depending on region.

Frame layout (25 bits):

    Byte 0:   System ID high (e.g. 0xFC)
    Byte 1:   System ID low  (e.g. 0xFF)
    Byte 2:   [pager:4][func:4]  -- upper nibble = pager, lower = function
    Bit 25:   Stop bit (always 1)

Pager function codes: 0xD = buzz/alert, 0xF = sync.

Each transmission sends ~40-50 repeated frames preceded by
one all-ones preamble frame (0xFFFFFF + stop bit).

Flex decoder:
    rtl_433 -f 315M  -X "n=pager,m=OOK_PWM,s=204,l=636,g=880,r=7312,bits=25"
    rtl_433 -f 433.92M -X "n=pager,m=OOK_PWM,s=204,l=636,g=880,r=7312,bits=25"
*/

#include "decoder.h"

static int restaurant_pager_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    for (int row = 0; row < bitbuffer->num_rows; row++) {
        uint8_t *b = bitbuffer->bb[row];
        unsigned bits = bitbuffer->bits_per_row[row];

        if (bits != 25)
            continue;

        // stop bit must be set
        if ((b[3] & 0x80) == 0)
            continue;

        int sys_id = (b[0] << 8) | b[1];
        int cmd    = b[2];
        int pager  = (cmd >> 4) & 0x0F;
        int func   = cmd & 0x0F;

        // skip preamble (all ones)
        if (b[0] == 0xFF && b[1] == 0xFF && b[2] == 0xFF)
            continue;

        if (sys_id == 0)
            continue;

        char const *func_str;
        switch (func) {
        case 0x0D: func_str = "Buzz";  break;
        case 0x0F: func_str = "Sync";  break;
        default:   func_str = "Other"; break;
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",    "",             DATA_STRING, "Restaurant-Pager",
                "id",       "System ID",    DATA_FORMAT, "%04X", DATA_INT, sys_id,
                "pager",    "Pager Code",   DATA_INT,    pager,
                "button",   "Function",     DATA_STRING, func_str,
                "code",     "Raw Command",  DATA_FORMAT, "%02X", DATA_INT, cmd,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);

        return 1;
    }

    return 0;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "pager",
        "button",
        "code",
        NULL,
};

r_device const restaurant_pager = {
        .name        = "Restaurant Pager (EV1527-variant, 25-bit)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 204,
        .long_width  = 636,
        .gap_limit   = 880,
        .reset_limit = 7312,
        .tolerance   = 180,
        .decode_fn   = &restaurant_pager_callback,
        .fields      = output_fields,
};
