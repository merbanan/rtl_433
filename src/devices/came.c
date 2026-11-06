/** @file
    CAME gate remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
CAME fixed code gate remotes, also Prastel and Airforce.

Seen on 433.92 MHz. PWM coded with a base time (TE) of 320 us: each bit is a gap
followed by a pulse, a 1 bit is a 640 us gap and a 320 us pulse, a 0 bit is a 320 us gap
and a 640 us pulse. A message starts with a 320 us start pulse after a long gap
(5 to 25 ms depending on the length) and is repeated.

Known lengths:

- 12 bit: CAME (12 DIP switches)
- 18 bit: Airforce
- 24 bit: CAME
- 25 bit: Prastel
- 42 bit: Prastel

Data layout:

    1 CCCC...

- 1: start pulse, always 1
- C: 12, 18, 24, 25 or 42 bit code

There is no integrity check. Each message is a package of its own (the gap between
messages ends the package), so a single message is decoded. Some ceiling fan remotes use
the same timing and 12 bit length and are decoded as CAME as well.

Flex decoder:

    rtl_433 -X 'n=came,m=OOK_PWM,s=320,l=640,g=2000,r=30000,t=150'
*/

static int came_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Prefer a repeated row, else use the first row
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 13);
    if (row < 0) {
        row = 0;
    }

    int bits = bitbuffer->bits_per_row[row] - 1; // without the start pulse
    char const *model;
    switch (bits) {
    case 12:
    case 24: model = "CAME-Remote"; break;
    case 18: model = "Airforce-Remote"; break;
    case 25:
    case 42: model = "Prastel-Remote"; break;
    default: return DECODE_ABORT_LENGTH;
    }

    if (!bitrow_get_bit(bitbuffer->bb[row], 0)) {
        return DECODE_ABORT_EARLY; // Start pulse is always 1
    }

    uint8_t b[6] = {0};
    bitbuffer_extract_bytes(bitbuffer, row, 1, b, bits);

    uint64_t code = 0;
    for (int i = 0; i < bits; ++i) {
        code = (code << 1) | ((b[i / 8] >> (7 - i % 8)) & 1);
    }

    // Reject all zero and all one codes
    if (code == 0 || code == ((uint64_t)1 << bits) - 1) {
        return DECODE_FAIL_SANITY;
    }

    char code_str[12];
    int digits = (bits + 3) / 4;
    for (int i = 0; i < digits; ++i) {
        code_str[i] = "0123456789ABCDEF"[(code >> (4 * (digits - 1 - i))) & 0xf];
    }
    code_str[digits] = '\0';

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",             DATA_STRING, model, // "CAME-Remote" "Airforce-Remote" "Prastel-Remote"
            "id",       "",             DATA_STRING, code_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        NULL,
};

r_device const came = {
        .name        = "CAME, Prastel, Airforce gate remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 320,
        .long_width  = 640,
        .gap_limit   = 2000,  // above the 640 us bit gap
        .reset_limit = 30000, // above the 25 ms message gap
        .tolerance   = 150,
        .decode_fn   = &came_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields,
};

/**
CAME TWEE gate remotes.

Seen on 433.92 MHz. Manchester coded with a half-bit time of 500 us, a 1 bit is high then low.
A button press sends 15 messages of 54 bits with a counter from 14 down to 0, each followed
by a 51 ms gap.

Data layout:

    FFFFFFFFFFFFFFFFFFFFFF XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

- F: 22 bit constant 0x3fff72
- X: 32 bit code XOR a value for the counter c, the low nibble of X is c

The XOR value for counter c is three bytes of 0x0e + 0x0f * c followed by the byte 0x11 * c.
In the de-XORed code D, the button is bits 6-9 and the 10 DIP switches are bits 18-27,
DIP switch 1 first.

The constant is the only check, there is no integrity check.

Flex decoder (half-bits, Manchester decode the 108 bits and invert):

    rtl_433 -X 'n=came_twee,m=OOK_PCM,s=500,l=500,r=6000,t=150'
*/

static int came_twee_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] < 107) {
            continue;
        }

        bitbuffer_t mc = {0};
        bitbuffer_manchester_decode(bitbuffer, row, 0, &mc, 54);
        if (mc.bits_per_row[0] != 54) {
            continue;
        }
        bitbuffer_invert(&mc);

        uint8_t b[7];
        bitbuffer_extract_bytes(&mc, 0, 0, b, 54);
        // Constant 0x3fff72 is the top 22 bits
        if (b[0] != 0xff || b[1] != 0xfd || (b[2] & 0xfc) != 0xc8) {
            decoder_log(decoder, 2, __func__, "constant not found");
            continue;
        }

        uint32_t x  = ((uint32_t)(b[2] & 0x03) << 30) | (b[3] << 22) | (b[4] << 14) | (b[5] << 6) | (b[6] >> 2);
        int counter = x & 0x0f;
        if (counter > 14) {
            continue;
        }
        uint8_t k  = 0x0e + 0x0f * counter;
        uint32_t d = x ^ (((uint32_t)k << 24) | (k << 16) | (k << 8) | ((0x11 * counter) & 0xff));
        int button = (d >> 6) & 0x0f;
        int dip    = (d >> 18) & 0x3ff;

        // DIP switch 1 is bit 18, make it the MSB
        int dip_id = 0;
        char dip_str[11];
        for (int i = 0; i < 10; ++i) {
            int on     = (dip >> i) & 1;
            dip_str[i] = on ? '1' : '0';
            dip_id     = (dip_id << 1) | on;
        }
        dip_str[10] = '\0';

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, "CAME-TWEE",
                "id",           "",             DATA_INT,    dip_id,
                "button",       "Button",       DATA_INT,    button,
                "dipswitch",    "DIP switches", DATA_STRING, dip_str,
                "counter",      "Counter",      DATA_INT,    counter,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return DECODE_ABORT_LENGTH;
}

static char const *const came_twee_output_fields[] = {
        "model",
        "id",
        "button",
        "dipswitch",
        "counter",
        NULL,
};

r_device const came_twee = {
        .name        = "CAME TWEE gate remotes",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 500,
        .long_width  = 500,
        .reset_limit = 6000, // below the 51 ms message gap
        .tolerance   = 150,
        .decode_fn   = &came_twee_decode,
        .disabled    = 1, // no integrity check
        .fields      = came_twee_output_fields,
};

/**
CAME Atomo gate remotes.

Seen on 433.92 MHz. Each parcel is a 1200 us pulse and a 600 us gap, then 60 bits
Manchester coded with a half-bit time of 600 us (a 1 bit is low then high), then a 68 ms gap.
A button press sends up to 8 parcels.

The 64 bit message M is sent without its low 4 bits (always 0).
Bits 8 to 58 of M are scrambled with a 7 bit shift register (feedback bit 3 XOR bit 4,
a set bit 7 flips the message bit) seeded with the negated hold counter.

Data layout after descrambling:

    HHHHHHHH CCCCCCCCCCCCCCCC SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS BBBB 0000

- H: 8 bit, byte 0 XOR 5 masked to 7 bits is the hold counter (counts while a button is held)
- C: 16 bit press counter
- S: 32 bit serial
- B: 4 bit button code, 0/2/4/6 for buttons 1 to 4

There is no integrity check, the button code is validated.

Flex decoder (half-bits, skip the 3 bit sync, Manchester decode 120 bits):

    rtl_433 -X 'n=came_atomo,m=OOK_PCM,s=600,l=600,r=10000,t=200'
*/

static int came_atomo_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        uint8_t *bb = bitbuffer->bb[row];
        int bits    = bitbuffer->bits_per_row[row];
        // 3 bit sync and 120 half-bits, then only the gap (the last low half-bit can merge into it)
        if (bits < 122 || bits > 140) {
            continue;
        }
        int trailing = 0;
        for (int i = 123; i < bits; ++i) {
            trailing |= bitrow_get_bit(bb, i);
        }
        if (trailing) {
            continue;
        }
        // Sync is a long pulse and a short gap
        if ((bb[0] & 0xe0) != 0xc0) {
            continue;
        }

        bitbuffer_t mc = {0};
        bitbuffer_manchester_decode(bitbuffer, row, 3, &mc, 60);
        if (mc.bits_per_row[0] != 60) {
            continue;
        }

        uint8_t m[8] = {0};
        bitbuffer_extract_bytes(&mc, 0, 0, m, 60);

        m[0]         = (m[0] ^ 5) & 0x7f;
        uint8_t lfsr = (uint8_t)(-m[0]) & 0x7f;
        for (int i = 8; i < 59; ++i) {
            int fb = ((lfsr >> 3) ^ (lfsr >> 4)) & 1;
            lfsr   = (uint8_t)(lfsr << 1) | fb;
            if (lfsr & 0x80) {
                m[i / 8] ^= 0x80 >> (i % 8);
            }
        }

        int btn_code = m[7] >> 4;
        if (btn_code & 0x09) {
            decoder_log(decoder, 2, __func__, "invalid button code");
            continue; // Only 0, 2, 4, 6 are known
        }

        int hold        = m[0];
        int counter     = (m[1] << 8) | m[2];
        uint32_t serial = ((uint32_t)m[3] << 24) | (m[4] << 16) | (m[5] << 8) | m[6];
        int button      = btn_code / 2 + 1;

        char id_str[9];
        snprintf(id_str, sizeof(id_str), "%08X", serial);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, "CAME-Atomo",
                "id",           "",             DATA_STRING, id_str,
                "button",       "Button",       DATA_INT,    button,
                "counter",      "Counter",      DATA_INT,    counter,
                "hold_counter", "Hold counter", DATA_INT,    hold,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return DECODE_ABORT_LENGTH;
}

static char const *const came_atomo_output_fields[] = {
        "model",
        "id",
        "button",
        "counter",
        "hold_counter",
        NULL,
};

r_device const came_atomo = {
        .name        = "CAME Atomo gate remotes",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 600,
        .long_width  = 600,
        .reset_limit = 10000, // below the 68 ms parcel gap
        .tolerance   = 200,
        .decode_fn   = &came_atomo_decode,
        .disabled    = 1, // no integrity check
        .fields      = came_atomo_output_fields,
};
