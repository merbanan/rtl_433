/** @file
    Hormann HSM and BiSecur garage door remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Hormann HSM fixed code garage door remotes, 44 bit.

The 868 MHz remotes with 32 bit codes (blue button) are decoded by conf/Hormann-blue.conf.

Seen on 433.92 MHz and 868 MHz. PWM coded, a 1 bit is a 1000 us pulse and a 500 us gap,
a 0 bit is a 500 us pulse and a 1000 us gap. A message starts with a 12 ms start pulse and a
500 us gap, about 20 messages are sent back to back.

Data layout:

    FFFFFFFF CCCCCCCCCCCCCCCCCCCCCCCC BBBB XXXXXX11

- F: 8 bit sync, always 0xff
- C: 24 bit code
- B: 4 bit button
- X: 8 bit, the last 2 bits are always 1

There is no integrity check, so two identical messages are required.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/hormann.c`.

Flex decoder:

    rtl_433 -X 'n=hormann_hsm,m=OOK_PWM,s=500,l=1000,g=3000,r=30000,t=250,invert,repeats>=2,bits=44'
*/

static int hormann_hsm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 44);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[row] != 44) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[row];
    // Sync byte and the 2 last bits
    if (b[0] != 0xff || (b[5] & 0x30) != 0x30) {
        return DECODE_ABORT_EARLY;
    }

    int button = (b[4] >> 4) & 0xf;

    char code_str[12];
    snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x%01x", b[0], b[1], b[2], b[3], b[4], b[5] >> 4);

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",         DATA_STRING, "Hormann-HSM",
            "id",       "",         DATA_STRING, code_str,
            "button",   "Button",   DATA_FORMAT, "%x", DATA_INT, button,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const hormann_hsm_output_fields[] = {
        "model",
        "id",
        "button",
        NULL,
};

r_device const hormann_hsm = {
        .name        = "Hormann HSM garage door remotes",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 500,
        .long_width  = 1000,
        .gap_limit   = 3000,  // above the 1000 us bit gap
        .reset_limit = 30000, // the start pulse starts a new row
        .tolerance   = 250,
        .decode_fn   = &hormann_hsm_decode,
        .disabled    = 1, // no integrity check
        .fields      = hormann_hsm_output_fields,
};

/**
Hormann BiSecur garage door remotes.

Seen on 868.3 MHz. FSK, Manchester coded with a half-bit time of 208 us, a 1 bit is high
then low. A message is a preamble of about 21 x (208 us low, 208 us high), a 1664 us high,
2 x (416 us low, 416 us high), then 176 data bits. A button press sends the message 3 times.

Data layout:

    TT SSSSSSSS EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE CC

- T: 8 bit type, 0x50 or 0x70
- S: 32 bit serial
- E: 128 bit encrypted (AES)
- C: 8 bit CRC

Type 0x50: CRC-8 poly 0x07 init 0x00 over the serial and the encrypted bytes, xor 0x55,
inverted. Type 0x70: reflected CRC-8 poly 0x07 init 0xff over the type, serial and encrypted
bytes.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/hormann_bisecur.c`.

Flex decoder:

    rtl_433 -X 'n=hormann_bisecur,m=FSK_PCM,s=208,l=208,r=3000,preamble={16}ff33'
*/

static int hormann_bisecur_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // 8 high slots of the 1664 us pulse, then 2 x (low, high) of 416 us
    uint8_t const preamble[] = {0xff, 0x33};

    int ret = DECODE_ABORT_EARLY;
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned pos = bitbuffer_search(bitbuffer, row, 0, preamble, 16);
        if (pos + 16 + 350 > bitbuffer->bits_per_row[row]) {
            continue;
        }

        bitbuffer_t msg = {0};
        bitbuffer_manchester_decode(bitbuffer, row, pos + 16, &msg, 176);
        if (msg.bits_per_row[0] < 175) {
            ret = DECODE_ABORT_LENGTH;
            continue;
        }
        bitbuffer_invert(&msg);

        uint8_t *b = msg.bb[0];

        int type = b[0];
        int crc;
        if (type == 0x50) {
            crc = ~(crc8(&b[1], 20, 0x07, 0x00) ^ 0x55) & 0xff;
        }
        else if (type == 0x70) {
            crc = crc8le(b, 21, 0x07, 0xff);
        }
        else {
            ret = DECODE_FAIL_SANITY;
            continue;
        }
        // The last half-bit can merge with the end of the message, the last CRC bit is then unknown
        if (msg.bits_per_row[0] == 175 && (crc & 0xfe) == (b[21] & 0xfe)) {
            b[21] = crc;
        }
        if (crc != b[21]) {
            ret = DECODE_FAIL_MIC;
            continue;
        }

        char serial[9];
        snprintf(serial, sizeof(serial), "%02x%02x%02x%02x", b[1], b[2], b[3], b[4]);

        char encrypted[33];
        for (int i = 0; i < 16; ++i) {
            snprintf(&encrypted[i * 2], 3, "%02x", b[5 + i]);
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, "Hormann-BiSecur",
                "id",           "",             DATA_STRING, serial,
                "msg_type",     "Type",         DATA_FORMAT, "%02x", DATA_INT, type,
                "encrypted",    "Encrypted",    DATA_STRING, encrypted,
                "mic",          "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return ret;
}

static char const *const hormann_bisecur_output_fields[] = {
        "model",
        "id",
        "msg_type",
        "encrypted",
        "mic",
        NULL,
};

r_device const hormann_bisecur = {
        .name        = "Hormann BiSecur garage door remotes",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 208,
        .long_width  = 208,
        .reset_limit = 3000,
        .decode_fn   = &hormann_bisecur_decode,
        .fields      = hormann_bisecur_output_fields,
};
