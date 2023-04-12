/** @file
    Decoder for UHF Dish Remote Control 6.3.

    Copyright (C) 2018 David E. Tiller

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Decoder for UHF Dish Remote Control 6.3.
(tested with genuine Dish remote.)

The device uses PPM encoding,
0 is encoded as 400 us pulse and 1692 uS gap,
1 is encoded as 400 us pulse and 2812 uS gap.
The device sends 7 transmissions per button press approx 6000 uS apart.
A transmission starts with a 400 uS start bit and a 6000 uS gap.

Each packet is 16 bits in length.
Packet bits: BBBBBB10 101X1XXX
B = Button pressed, big-endian
X = unknown, possibly channel
*/

#include "decoder.h"

#define MYDEVICE_BITLEN      16
#define MYDEVICE_MINREPEATS  3

char const *button_map[] = {
/*  0 */ "Undefined",
/*  1 */ "Undefined",
/*  2 */ "Swap",
/*  3 */ "Undefined",
/*  4 */ "Position",
/*  5 */ "PIP",
/*  6 */ "DVR",
/*  7 */ "Undefined",
/*  8 */ "Skip Forward",
/*  9 */ "Skip Backward",
/* 10 */ "Undefined",
/* 11 */ "Dish Button",
/* 12 */ "Undefined",
/* 13 */ "Forward",
/* 14 */ "Backward",
/* 15 */ "TV Power",
/* 16 */ "Reset",
/* 17 */ "Undefined",
/* 18 */ "Undefined",
/* 19 */ "Undefined",
/* 20 */ "Undefined",
/* 21 */ "Undefined",
/* 22 */ "SAT",
/* 23 */ "Mute/Volume Up/Volume Down",
/* 24 */ "Undefined",
/* 25 */ "#/Search",
/* 26 */ "*/Format",
/* 27 */ "Undefined",
/* 28 */ "Undefined",
/* 29 */ "Undefined",
/* 30 */ "Stop",
/* 31 */ "Pause",
/* 32 */ "Record",
/* 33 */ "Channel Down",
/* 34 */ "Undefined",
/* 35 */ "Left",
/* 36 */ "Recall",
/* 37 */ "Channel Up",
/* 38 */ "Undefined",
/* 39 */ "Right",
/* 40 */ "TV/Video",
/* 41 */ "View/Live TV",
/* 42 */ "Undefined",
/* 43 */ "Guide",
/* 44 */ "Undefined",
/* 45 */ "Cancel",
/* 46 */ "Digit 0",
/* 47 */ "Select",
/* 48 */ "Page Up",
/* 49 */ "Digit 9",
/* 50 */ "Digit 8",
/* 51 */ "Digit 7",
/* 52 */ "Menu",
/* 53 */ "Digit 6",
/* 54 */ "Digit 5",
/* 55 */ "Digit 4",
/* 56 */ "Page Down",
/* 57 */ "Digit 3",
/* 58 */ "Digit 2",
/* 59 */ "Digit 1",
/* 60 */ "Play",
/* 61 */ "Dish Power",
/* 62 */ "Undefined",
/* 63 */ "Info"
};

static int dish_remote_6_3_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int r; // a row index
    uint8_t *b; // bits of a row
    uint8_t button;
    char const *button_string;

    decoder_log_bitbuffer(decoder, 2, __func__, bitbuffer, "");

    r = bitbuffer_find_repeated_row(bitbuffer, MYDEVICE_MINREPEATS, MYDEVICE_BITLEN);
    if (r < 0 || bitbuffer->bits_per_row[r] > MYDEVICE_BITLEN) {
        return DECODE_ABORT_LENGTH;
    }

    b = bitbuffer->bb[r];

    /* Check fixed bits to prevent misreads */
    if ((b[0] & 0x03) != 0x02 || (b[1] & 0xe8) != 0xa8) {
        return DECODE_FAIL_SANITY;
    }

    button = b[0] >> 2;
    button_string = button_map[button];

    /* clang-format off */
    data = data_make(
            "model",    "",     DATA_STRING, "Dish-RC63",
            "button",   "",     DATA_STRING, button_string,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "button",
        NULL,
};

r_device const dish_remote_6_3 = {
        .name        = "Dish remote 6.3",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1692,
        .long_width  = 2812,
        .gap_limit   = 4500,
        .reset_limit = 9000,
        .decode_fn   = &dish_remote_6_3_callback,
        .disabled    = 1,
        .fields      = output_fields,
};
