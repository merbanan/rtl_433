/** @file
    Holtek HT640 and HT12E encoder remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Holtek HT640 encoder remotes, e.g. Linx CMD-HHLR.

The HT640 is a 3^18 tristate encoder with 10 address pins (A0-A9) and 8 address/data pins
(AD10-AD17). The HT680 of the same series is decoded by the HT680-Remote decoder.

Seen on 433.92 MHz. PWM coded, each bit is a gap followed by a pulse, a 1 bit is a 870 us
gap and a 430 us pulse, a 0 bit is a 430 us gap and a 870 us pulse (about 920 us measured).
A message starts with a 430 us start pulse after a 15 ms gap and is repeated.
The pulse windows (270-590 us, 760-1080 us) leave out HT680 remotes (260 us, 660 us), which
send the same message format.

Data layout:

    1 0101 AAAAAAAAAAAAAAAAAAAA DDDDDDDDDDDDDDDD

- 1: start pulse, always 1
- 0101: sync
- A: A0-A9, 2 bits per pin
- D: AD10-AD17, 2 bits per pin

Each pin is 00 (low), 11 (high) or 10 (floating), 01 is invalid.
The id is the 20 address bits in reverse order. The data pins are 4 nibbles, a floating pair
(0xa) is idle. The first nibble from the end that is not idle is the button, 0xe is on and
0xb is off.

There is no integrity check. Each message is a package of its own (the gap between
messages ends the package), so a single message is decoded.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/holtek.c`.

Flex decoder:

    rtl_433 -X 'n=ht640,m=OOK_PWM,s=430,l=920,g=1500,r=14000,t=160'
*/

static int holtek_ht640_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = DECODE_ABORT_LENGTH;
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] != 41) {
            continue;
        }

        uint8_t *bb = bitbuffer->bb[row];
        // Start pulse and sync
        if ((bb[0] & 0xf8) != 0xa8) {
            ret = DECODE_ABORT_EARLY;
            continue;
        }

        uint8_t b[5];
        bitbuffer_extract_bytes(bitbuffer, row, 5, b, 36);

        // Tristate pins, 01 is invalid
        char tristate[19];
        int valid = 1;
        for (int i = 0; i < 18; ++i) {
            int pin = (b[i / 4] >> (6 - 2 * (i % 4))) & 0x3;
            if (pin == 0x1) {
                valid = 0;
                break;
            }
            tristate[i] = "0XZ1"[pin];
        }
        if (!valid) {
            ret = DECODE_FAIL_SANITY;
            continue;
        }
        tristate[18] = '\0';

        int addr = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);
        int id   = 0;
        for (int i = 0; i < 20; ++i) {
            id = (id << 1) | ((addr >> i) & 1);
        }

        int data   = ((b[2] & 0x0f) << 12) | (b[3] << 4) | (b[4] >> 4);
        int button = 0;
        int nibble = 0;
        for (int i = 0; i < 4; ++i) {
            nibble = (data >> (4 * i)) & 0xf;
            if (nibble != 0xa) {
                button = i + 1;
                break;
            }
        }
        char const *state = "";
        if (button && nibble == 0xe) {
            state = "on";
        }
        else if (button && nibble == 0xb) {
            state = "off";
        }

        /* clang-format off */
        data_t *output = data_make(
                "model",    "",                 DATA_STRING, "Holtek-HT640",
                "id",       "",                 DATA_FORMAT, "%05x", DATA_INT, id,
                "button",   "Button",           DATA_INT,    button,
                "state",    "State",            DATA_COND,   *state, DATA_STRING, state,
                "tristate", "Tristate code",    DATA_STRING, tristate,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, output);
        return 1;
    }
    return ret;
}

static char const *const holtek_ht640_output_fields[] = {
        "model",
        "id",
        "button",
        "state",
        "tristate",
        NULL,
};

r_device const holtek_ht640 = {
        .name        = "Holtek HT640 remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 430,
        .long_width  = 920,
        .gap_limit   = 1500,  // above the 870 us bit gap
        .reset_limit = 14000, // below the 15 ms message gap
        .tolerance   = 160,
        .decode_fn   = &holtek_ht640_decode,
        .disabled    = 1, // no integrity check
        .fields      = holtek_ht640_output_fields,
};

/**
Holtek HT12E (and HT12A) encoder remotes.

The HT12E is a 2^12 encoder with 8 address pins (A0-A7) and 4 data pins (D8-D11). The data
pins are usually the buttons, pulled high, a pressed button is 0.

Seen on 433.92 MHz. PWM coded with a base time (TE) that depends on the oscillator resistor,
about 200 us: each bit is a gap followed by a pulse, a 1 bit is a 2 TE gap and a 1 TE pulse,
a 0 bit is a 1 TE gap and a 2 TE pulse. A message starts with a 1 TE start pulse after a
gap of about 35 TE and is repeated. Pulses are split at 300 us, which covers TE from about
180 to 250 us. Slower remotes (TE above about 280 us) send each message as a package of its
own and are not decoded here, e.g. Harbor Breeze FAN-11T (see conf/fan-11t.conf).

Data layout:

    1 AAAAAAAA DDDD

- 1: start pulse, always 1
- A: 8 bit address
- D: 4 bit data

There is no integrity check, so two identical messages are required. The repeats are in one
package, the gap between messages starts a new row.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/holtek_ht12x.c`.

Flex decoder:

    rtl_433 -X 'n=ht12e,m=OOK_PWM,s=200,l=400,g=1500,r=20000,repeats>=2,bits=13'
*/

static int holtek_ht12e_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 13);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[row] != 13) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *bb = bitbuffer->bb[row];
    if (!(bb[0] & 0x80)) {
        return DECODE_ABORT_EARLY; // Start pulse is always 1
    }

    uint8_t b[2];
    bitbuffer_extract_bytes(bitbuffer, row, 1, b, 12);
    int code = (b[0] << 4) | (b[1] >> 4);
    // Reject all zero and all one codes, also the result of a TE out of range
    if (code == 0 || code == 0xfff) {
        return DECODE_FAIL_SANITY;
    }

    int id     = code >> 4;
    int button = code & 0xf;

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",         DATA_STRING, "Holtek-HT12E",
            "id",       "",         DATA_FORMAT, "%02x", DATA_INT, id,
            "button",   "Button",   DATA_FORMAT, "%x", DATA_INT, button,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const holtek_ht12e_output_fields[] = {
        "model",
        "id",
        "button",
        NULL,
};

r_device const holtek_ht12e = {
        .name        = "Holtek HT12E remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 200,
        .long_width  = 400,
        .gap_limit   = 1500,  // above the 2 TE bit gap
        .reset_limit = 20000, // above the message gap
        .decode_fn   = &holtek_ht12e_decode,
        .disabled    = 1, // no integrity check
        .fields      = holtek_ht12e_output_fields,
};
