/** @file
    Phoenix V2 gate remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Phoenix V2 rolling code gate remotes.

Seen on 433.92 MHz. A message is a 25.6 ms gap, a 2560 us start pulse, then 52 bits. Each
bit is a gap followed by a pulse, a 1 bit is a 427 us gap and an 853 us pulse, a 0 bit is an
853 us gap and a 427 us pulse. The message is repeated.

The 52 bits sent are the bit reverse of a 56 bit value (the 4 lowest bits are not sent, 0):

    CCCC 0B SSSSSSSS

- C: 16 bit counter, scrambled
- B: 4 bit button (the 4 bits above it vary)
- S: 32 bit serial, the lowest 4 bits are 0

The counter is scrambled with the two high serial bytes as key: 16 rounds of a 1 bit shift
across the two counter bytes, with the serial bytes xored in when the shifted out bit is 0.

There is no integrity check. Two identical messages are preferred, a single message is only
accepted right after the start pulse.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/phoenix_v2.c`.

Flex decoder:

    rtl_433 -X 'n=phoenix_v2,m=OOK_PWM,s=427,l=853,y=2560,g=1500,r=30000,t=250,invert,bits=52'
*/

static uint16_t phoenix_v2_counter(uint16_t scrambled, uint32_t serial)
{
    uint8_t b1 = scrambled >> 8;
    uint8_t b2 = scrambled & 0xff;
    uint8_t k1 = serial >> 24;
    uint8_t k2 = serial >> 16;
    for (int i = 0; i < 16; ++i) {
        int msb = b1 & 0x80;
        int lsb = b2 & 0x01;
        b2      = (uint8_t)((b2 >> 1) | msb);
        b1      = (uint8_t)((b1 << 1) | lsb);
        if (!msb) {
            b1 ^= k1;
            b2 = (b2 ^ k2) & 0x7f;
        }
    }
    return (uint16_t)((b2 << 8) | b1);
}

static int phoenix_v2_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    // Prefer a repeated message, else a single message right after a start pulse
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 52);
    for (int i = 0; row < 0 && i < bitbuffer->num_rows; ++i) {
        if (bitbuffer->bits_per_row[i] == 52 && bitbuffer->syncs_before_row[i] > 0) {
            row = i;
        }
    }
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[row] != 52) {
        return DECODE_ABORT_LENGTH;
    }

    // Bit reverse of the 52 bits, as a 56 bit value
    uint64_t full = 0;
    for (int i = 0; i < 52; ++i) {
        full |= (uint64_t)bitrow_get_bit(bitbuffer->bb[row], i) << (i + 4);
    }

    uint32_t serial = full & 0xffffffff;
    int button      = (full >> 32) & 0xf;
    int counter     = phoenix_v2_counter((full >> 40) & 0xffff, serial);

    if (serial == 0 || serial == 0xfffffff0) {
        return DECODE_FAIL_SANITY;
    }

    char serial_str[9];
    snprintf(serial_str, sizeof(serial_str), "%08X", serial);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",         DATA_STRING, "Phoenix-V2",
            "id",           "",         DATA_STRING, serial_str,
            "button",       "Button",   DATA_INT,    button,
            "counter",      "Counter",  DATA_INT,    counter,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        "counter",
        NULL,
};

r_device const phoenix_v2 = {
        .name        = "Phoenix V2 gate remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 427,
        .long_width  = 853,
        .sync_width  = 2560,
        .tolerance   = 250,
        .gap_limit   = 1500,  // above the 853 us bit gap
        .reset_limit = 30000, // above the 25.6 ms message gap
        .decode_fn   = &phoenix_v2_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields,
};
