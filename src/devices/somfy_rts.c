/** @file
    Somfy RTS.

    Copyright (C) 2020 Matthias Schulz <mschulz@seemoo.tu-darmstadt.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Somfy RTS.

Protocol description:
The protocol is very well defined under the following two links:
[1] https://pushstack.wordpress.com/somfy-rts-protocol/
[2] https://patentimages.storage.googleapis.com/bd/ae/4f/bf24e41e0161ca/US8189620.pdf

Each frame consists of a preamble with hardware and software sync pulses followed by the manchester encoded data pulses.
A rising edge describes a data bit 1 and a falling edge a data bit 0. The preamble is different for the first frame and
for retransmissions. In the end, the signal is first decoded using an OOK PCM decoder and within the callback, only the
data bits will be manchester decoded.

In the following, each character representing a low level "_" and a high level "^" is roughly 604 us long.

First frames' preamble:

    ^^^^^^^^^^^^^^^^___________^^^^____^^^^____^^^^^^^^_

The first long pulse is often wrongly detected, so I just make sure that it ends up in another row during decoding and
then only consider the rows containing the second part of the first frame preamble.

Retransmission frames' preamble:

    ^^^^____^^^^____^^^^____^^^^____^^^^____^^^^____^^^^____^^^^^^^^_

On some devices (see #2356) the preamble is two bytes shorter apparently?

The data is manchester encoded _^ represents a 1 and ^_ represents a 0. The data section consists of 56 bits that equals
7 bytes of scrambled data. The data is scrambled by XORing each following byte with the last scrambled byte. After
descrambling, the 7 bytes have the following meaning conting byte from left to right as in big endian byte order:

- byte 0:   called "random" in [1] and "key" in [2], in the end it is just the seed for the scrambler
- byte 1:   The higher nibble represents the control command, the lower nibble is the frame's checksum calculated by XORing
            all nibbles
- byte 2-3: Replay counter value in big endian byte order
- byte 4-6: Remote control channel's address

On some devices (see #2356) there are two extra bytes for a total of 80 bits apparently?

## TEL-FIX wall-mounted remote control for RadioLoop Motor

There is a quirk with TEL-FIX wall-mounted remote control for RadioLoop Motor:
It looks like the seed isn't random but actually the button code: 0x88 DOWN, 0x85 STOP, 0x86 UP.
The command is fixed to 0xf, which we use as indication that an actual command is in the seed.

*/

#include "decoder.h"

static int somfy_rts_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    char const *const control_strs[] = {
            "? (0)",
            "My (1)",
            "Up (2)",
            "My + Up (3)",
            "Down (4)",
            "My + Down (5)",
            "Up + Down (6)",
            "My + Up + Down (7)",
            "Prog (8)",
            "Sun + Flag (9)",
            "Flag (10)",
            "? (11)",
            "? (12)",
            "? (13)",
            "? (14)",
            "? (15)",
    };

    char const *const seed_strs[] = {
            "? (0)",
            "? (1)",
            "? (2)",
            "? (3)",
            "? (4)",
            "Stop (5)",
            "Up (6)",
            "? (7)",
            "Down (8)",
            "? (9)",
            "? (10)",
            "? (11)",
            "? (12)",
            "? (13)",
            "? (14)",
            "? (15)",
    };

    // full retransmission pattern is {65}f0f0f0f0f0f0f0ff0
    // some devices only have 49 bit preamble, don't require the first 16 bit
    uint8_t const preamble_pattern_long[] = {0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xff, 0x00};
    uint8_t const preamble_length_long = 49;
    // alternate pattern if the bitrate wrongly shortens the 8x 1's to 7x.
    uint8_t const preamble_pattern_rate[] = {0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xfe, 0x00};
    uint8_t const preamble_length_rate = 48;
    // full fist transmission pattern after sync is {25}f0f0ff0
    uint8_t const preamble_pattern_short[] = {0xf0, 0xf0, 0xff, 0x00};
    uint8_t const preamble_length_short = 25;

    int is_retransmission = 0;
    int decode_row = -1;
    int bitpos = 0;

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        if (bitbuffer->bits_per_row[row] > 170) {
            is_retransmission = 1;
            bitpos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern_long, preamble_length_long) + preamble_length_long;
            // Retry for wrong bitrate if needed
            if (bitpos + 56 * 2 > bitbuffer->bits_per_row[row]) {
                bitpos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern_rate, preamble_length_rate) + preamble_length_rate;
            }
            // Are there at least 56 MC bits in this row?
            if (bitpos + 56 * 2 <= bitbuffer->bits_per_row[row]) {
                decode_row = row;
                break;
            }
        }
        else if (bitbuffer->bits_per_row[row] > 130) {
            is_retransmission = 0;
            bitpos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern_short, preamble_length_short) + preamble_length_short;
            if (bitpos + 56 * 2 <= bitbuffer->bits_per_row[row]) {
                decode_row = row;
                break;
            }
        }
    }

    if (decode_row < 0)
        return DECODE_ABORT_EARLY;

    // Are there at least 56 MC bits in this row?
    if (bitpos + 56 * 2 > bitbuffer->bits_per_row[decode_row])
        return DECODE_ABORT_LENGTH;

    bitbuffer_t decoded = {0};
    bitbuffer_manchester_decode(bitbuffer, decode_row, bitpos, &decoded, 80);
    if (decoded.num_rows == 0 || decoded.bits_per_row[0] < 56)
        return DECODE_ABORT_LENGTH;

    uint8_t *b = decoded.bb[0];

    // descramble
    for (int i = 6; i > 0; i--)
        b[i] = b[i] ^ b[i - 1];

    // calculate and verify checksum
    int chksum_calc = xor_bytes(b, 7);
    chksum_calc = (chksum_calc & 0xf) ^ (chksum_calc >> 4); // fold to nibble
    if (chksum_calc != 0)
        return DECODE_FAIL_MIC;

    int seed    = b[0];
    int control = (b[1] & 0xf0) >> 4;
    int chksum  = b[1] & 0xf;
    int counter = (b[2] << 8) | b[3];
    // assume little endian as multiple addresses used by one remote control increase the address value in little endian byte order.
    int address = (b[6] << 16) | (b[5] << 8) | b[4];

    // lookup control
    char const *control_str = control_strs[control];
    if (control == 0xf) {
        // TEL-FIX quirk
        control_str = seed_strs[seed & 0xf];
    }

    decoder_logf(decoder, 2, __func__, "seed=0x%02x, chksum=0x%x", seed, chksum);

    /* clang-format off */
    data_t *data = data_make(
            "model",          "",               DATA_STRING, "Somfy-RTS",
            "id",             "",               DATA_FORMAT, "%06X", DATA_INT, address,
            "control",        "Control",        DATA_STRING, control_str,
            "counter",        "Counter",        DATA_INT,    counter,
            "retransmission", "Retransmission", DATA_INT,    is_retransmission,
            "mic",            "Integrity",      DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "control",
        "counter",
        "retransmission",
        "mic",
        NULL,
};

// rtl_433 -r g001_433.414M_250k.cu8 -X "n=somfy-test,m=OOK_PCM,s=604,l=604,t=40,r=10000,g=3000,y=2416"
// Nominal bit width is ~604 us, RZ, short=long

r_device const somfy_rts = {
        .name        = "Somfy RTS",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 604,   // each pulse is ~604 us (nominal bit width)
        .long_width  = 604,   // each pulse is ~604 us (nominal bit width)
//        .sync_width     = 2416,  // hardware sync pulse is ~2416 us (4 x nominal bit width), software sync pulse is ~4550 us. Commented, as sync_width has no effect on the PCM decoder.
        .gap_limit   = 3000,  // largest off between two pulses is ~2416 us during sync. Gap between start pulse (9664 us) and first frame is 6644 us (11 x nominal bit width), 3000 us will split first message into two rows one with start pulse and one with first frame
        .reset_limit = 10000, // larger than gap between start pulse and first frame (6644 us = 11 x nominal bit width) to put start pulse and first frame in two rows, but smaller than inter-frame space of 30415 us
        .tolerance   = 20,
        .decode_fn   = &somfy_rts_decode,
        .fields      = output_fields,
};
