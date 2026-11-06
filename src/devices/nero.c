/** @file
    Nero Radio and Nero Sketch blind and shutter remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/// First row of the given length after a preamble row (at least 40 short pulses, 0 bits),
/// that is sent at least twice, or -1.
static int nero_find_repeated_row(bitbuffer_t *bitbuffer, unsigned bits)
{
    for (int row = 1; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] != bits || bitbuffer->bits_per_row[row - 1] < 40) {
            continue;
        }
        uint8_t *p = bitbuffer->bb[row - 1];
        if (p[0] | p[1] | p[2] | p[3] | p[4]) {
            continue;
        }
        if (bitbuffer_count_repeats(bitbuffer, row, 0) >= 2) {
            return row;
        }
    }
    return -1;
}

/**
Nero Sketch blind and shutter remotes.

Seen on 433.92 MHz. A message is 47 x (330 us pulse, 330 us gap), a 1320 us start pulse, then
40 bits and a 990 us stop pulse. PWM coded, a 1 bit is a 660 us pulse and a 330 us gap, a 0 bit
is a 330 us pulse and a 660 us gap. The message is repeated.

The layout of the 40 bit code is not known, the code is reported as is.

There is no integrity check, so two identical messages are required, each after the preamble.
The preamble tells it from Dooya remotes (single 4.8 ms start pulse, similar timing, 40 bits).

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/nero_sketch.c`.
*/

static int nero_sketch_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    // The 47 bit preamble rows are longer than the code
    int row = nero_find_repeated_row(bitbuffer, 40);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *b = bitbuffer->bb[row];
    // Reject all zero and all one codes
    if ((b[0] | b[1] | b[2] | b[3] | b[4]) == 0 || (b[0] & b[1] & b[2] & b[3] & b[4]) == 0xff) {
        return DECODE_FAIL_SANITY;
    }

    char code_str[11];
    snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4]);

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",     DATA_STRING, "Nero-Sketch",
            "code",     "",     DATA_STRING, code_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const nero_sketch_output_fields[] = {
        "model",
        "code",
        NULL,
};

r_device const nero_sketch = {
        .name        = "Nero Sketch blind remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 330,
        .long_width  = 660,
        .gap_limit   = 1500,
        .reset_limit = 10000,
        .tolerance   = 150,
        .decode_fn   = &nero_sketch_decode,
        .disabled    = 1, // no integrity check
        .fields      = nero_sketch_output_fields,
};

/**
Nero Radio blind and shutter remotes.

Seen on 433.92 MHz. A message is 49 x (200 us pulse, 200 us gap), an 830 us start pulse, then
56 bits and a 4.6 ms gap. PWM coded, a 1 bit is a 400 us pulse and a 200 us gap, a 0 bit is a
200 us pulse and a 400 us gap. The message is repeated. Some remotes send 57 bits with a 1.3 ms
gap (decoded the same way, counted from the last bit, no sample).

Data layout (56 bits):

    IIIIIII B SSSS CC

- I: 28 bit serial, high part
- B: 4 bit button
- S: 16 bit serial, low part
- C: 8 bit, unknown, changes with the button (maybe a checksum)

The id is the high and low serial parts together.

There is no known integrity check, so two identical messages are required, each after the
preamble. The preamble tells it from Acurite 592TXR sensors (4 sync pulses, same timing, 56 bits).

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/nero_radio.c`.
*/

static int nero_radio_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    unsigned bits = 56;
    int row       = nero_find_repeated_row(bitbuffer, 56);
    if (row < 0) {
        bits = 57;
        row  = nero_find_repeated_row(bitbuffer, 57);
    }
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    // Value of the last 56 bits, a 57 bit message has one more leading bit
    uint8_t b[7];
    bitbuffer_extract_bytes(bitbuffer, row, bits - 56, b, 56);
    int extra = bits == 57 ? bitrow_get_bit(bitbuffer->bb[row], 0) : 0;

    uint32_t serial_hi = ((uint32_t)extra << 28) | ((uint32_t)b[0] << 20) | (b[1] << 12) | (b[2] << 4) | (b[3] >> 4);
    int button         = b[3] & 0x0f;
    int serial_lo      = (b[4] << 8) | b[5];
    int check          = b[6];

    if (serial_hi == 0 && serial_lo == 0) {
        return DECODE_FAIL_SANITY;
    }

    char id_str[13];
    snprintf(id_str, sizeof(id_str), "%x%04x", serial_hi, serial_lo);

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",         DATA_STRING, "Nero-Radio",
            "id",       "",         DATA_STRING, id_str,
            "button",   "Button",   DATA_INT,    button,
            "check",    "Check",    DATA_FORMAT, "%02x", DATA_INT, check,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const nero_radio_output_fields[] = {
        "model",
        "id",
        "button",
        "check",
        NULL,
};

r_device const nero_radio = {
        .name        = "Nero Radio blind remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 200,
        .long_width  = 400,
        .gap_limit   = 1000,
        .reset_limit = 10000,
        .tolerance   = 100,
        .decode_fn   = &nero_radio_decode,
        .disabled    = 1, // no integrity check
        .fields      = nero_radio_output_fields,
};
