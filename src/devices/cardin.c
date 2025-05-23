/** @file
    Cardin S466-TX2 generic garage door remote control on 27.195 Mhz.

    Copyright (C) 2018 Christian W. Zuckschwerdt <zany@triq.net>
    original implementation 2015 Denis Bodor

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Cardin S466-TX2 generic garage door remote control on 27.195 Mhz.

Note: Similar to an EV1527 / SC2260, but there is a 6152 us sync pulse first, then 24 bit of 732 us / 1412 us leading-gap PWM.
Decodes to 9 tri-state DIP-switches and a 2-bit button.

Remember to set the correct freq with -f 27.195M
May be useful for other Cardin product too

- "11R"  = on-on    Right button used
- "10R"  = on-off   Right button used
- "01R"  = off-on   Right button used
- "00L?" = off-off  Left button used or right button does the same as the left
*/
static int cardin_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] != 24) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[0];

    // validate message as best as we can
    // constrain b[2] & 0x3f (the button) to 0x03, 0x06, 0x09, 0x0c
    if ((b[2] & 0x3f) != 0x03
            && (b[2] & 0x3f) != 0x09
            && (b[2] & 0x3f) != 0x0c
            && (b[2] & 0x3f) != 0x06) {
        return DECODE_ABORT_EARLY;
    }
    // Disallow the fourth tri-state option on the 9 DIP switches
    if (((b[0] & 8) == 0 && (b[1] & 8) != 0)
            || ((b[0] & 16) == 0 && (b[1] & 16) != 0)
            || ((b[0] & 32) == 0 && (b[1] & 32) != 0)
            || ((b[0] & 64) == 0 && (b[1] & 64) != 0)
            || ((b[0] & 128) == 0 && (b[1] & 128) != 0)
            || ((b[2] & 128) == 0 && (b[2] & 64) != 0)
            || ((b[0] & 1) == 0 && (b[1] & 1) != 0)
            || ((b[0] & 2) == 0 && (b[1] & 2) != 0)
            || ((b[0] & 4) == 0 && (b[1] & 4) != 0)) {
        return DECODE_ABORT_EARLY;
    }

    // Get button code
    char const *const rbutton[4] = { "11R", "10R", "01R", "00L?" };
    char const *const button = rbutton[((b[2] & 0x0f) / 3) - 1];

    // Get DIP tri-state switches
    char dip[10] = {'-','-','-','-','-','-','-','-','-', '\0'};

    // Dip 1
    if (b[0] & 8) {
        dip[0] = 'o';
        if (b[1] & 8)
            dip[0] = '+';
    }
    // Dip 2
    if (b[0] & 16) {
        dip[1] = 'o';
        if (b[1] & 16)
            dip[1] = '+';
    }
    // Dip 3
    if (b[0] & 32) {
        dip[2] = 'o';
        if (b[1] & 32)
            dip[2] = '+';
    }
    // Dip 4
    if (b[0] & 64) {
        dip[3] = 'o';
        if (b[1] & 64)
            dip[3] = '+';
    }
    // Dip 5
    if (b[0] & 128) {
        dip[4] = 'o';
        if (b[1] & 128)
            dip[4] = '+';
    }
    // Dip 6
    if (b[2] & 128) {
        dip[5] = 'o';
        if (b[2] & 64)
            dip[5] = '+';
    }
    // Dip 7
    if (b[0] & 1) {
        dip[6] = 'o';
        if (b[1] & 1)
            dip[6] = '+';
    }
    // Dip 8
    if (b[0] & 2) {
        dip[7] = 'o';
        if (b[1] & 2)
            dip[7] = '+';
    }
    // Dip 9
    if (b[0] & 4) {
        dip[8] = 'o';
        if (b[1] & 4)
            dip[8] = '+';
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",      "",                       DATA_STRING, "Cardin-S466",
            "dipswitch",  "dipswitch",              DATA_STRING, dip,
            "rbutton",    "right button switches",  DATA_STRING, button,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "dipswitch",
        "rbutton",
        NULL,
};

r_device const cardin = {
        .name        = "Cardin S466-TX2",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 730,
        .long_width  = 1400,
        .sync_width  = 6150,
        .gap_limit   = 1600,
        .reset_limit = 32000,
        .decode_fn   = &cardin_decode,
        .fields      = output_fields,
};
