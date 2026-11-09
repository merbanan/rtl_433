/** @file
    Hormann BiSecur garage door remotes.

    Copyright (C) 2026 Cristi Libotean <eblis102@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

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

The HSM fixed code remotes (433.92 MHz, 44 bit) are decoded by conf/Hormann-HSM.conf, the 868 MHz
remotes with 32 bit codes (blue button) by conf/Hormann-blue.conf.

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
