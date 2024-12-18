/** @file
    Microchip HCS362 KeeLoq Code Hopping Encoder based remotes.

    Copyright (C) 2024 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Microchip HCS362 KeeLoq Code Hopping Encoder based remotes.

There are two transmissions modes: PWM (mode 0) and MC (mode 1).

For MC with start+stop bit 71 bits are transmitted, LSB first.

69-bit transmission code length
- 32-bit hopping code
- 37-bit fixed code (28/32-bit serial number, 4/0-bit function code, 1-bit status, 2-bit CRC/time, 2-bit queue)
- Stop bit

|  0-31 | 32 bit Encrypted Portion
| 32-59 | 28 bit Serial Number
| 60-63 | 4 bit Function Code (S3, S0, S1, S2)
| 64    | 1 bit Battery Low (Low Voltage Detector Status)
| 65-66 | 2 bit CRC
| 67-68 | 2 bit Button Queue Information

Note that the button bits are (MSB/first sent to LSB) S3, S0, S1, S2.
Hardware buttons might map to combinations of these bits.

- Datasheet HCS362: https://ww1.microchip.com/downloads/aemDocuments/documents/MCU08/ProductDocuments/DataSheets/40189E.pdf

The preamble of 12 short pulses is followed by a long sync gap.

Raw data capture:

    rtl_433 -R 0 -X 'n=HCS362,m=OOK_PCM,s=214,l=214,g=600,r=900'
*/

static int hcs362_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Check preamble
    if (bitbuffer->bits_per_row[0] < 12 * 2 - 8 || bitbuffer->bits_per_row[0] > 12 * 2 + 8) {
        decoder_log(decoder, 2, __func__, "Preamble not found");
        return DECODE_ABORT_LENGTH;
    }
    // Reject codes with an incorrect preamble (expected 0xaaaaaa)
    uint8_t *b = bitbuffer->bb[0];
    if (b[0] != 0xaa || b[1] != 0xaa || b[2] != 0xaa) {
        decoder_log(decoder, 2, __func__, "Preamble invalid");
        return DECODE_ABORT_EARLY;
    }
    // Reject codes of wrong length
    if (bitbuffer->bits_per_row[1] < 72 * 2 || bitbuffer->bits_per_row[1] > 72 * 2 + 4) {
        return DECODE_ABORT_LENGTH;
    }

    // Second row is data
    b = bitbuffer->bb[1];
    // Check for the start bit
    if ((b[0] & 0xc0) != 0x80) {
        decoder_log(decoder, 2, __func__, "Startbit not found");
        return DECODE_ABORT_EARLY;
    }
    // Manchester decode, excluding startbit
    bitbuffer_t msg = {0};
    unsigned len = bitbuffer_manchester_decode(bitbuffer, 1, 2, &msg, 72);
    decoder_log_bitbuffer(decoder, 1, __func__, &msg, "Decoded");

    // Reject codes of wrong length
    if (len < 69 + 1) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_invert(&msg); // want G.E.Thomas, not IEEE 802.3
    b = msg.bb[0];
    // No need to decode/extract values for simple test
    if (b[1] == 0xff && b[2] == 0xff && b[3] == 0xff && b[4] == 0xff
            && b[5] == 0xff && b[6] == 0xff && b[7] == 0xff) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0xff");
        return DECODE_FAIL_SANITY;
    }

    // The transmission is LSB first, big endian.
    uint32_t encrypted = ((unsigned)reverse8(b[3]) << 24) | (reverse8(b[2]) << 16) | (reverse8(b[1]) << 8) | (reverse8(b[0]));
    int serial         = (reverse8(b[7] & 0xf0) << 24) | (reverse8(b[6]) << 16) | (reverse8(b[5]) << 8) | (reverse8(b[4]));
    int btn            = (b[7] & 0x0f);
    int btn_num        = (btn & 0x08) | ((btn & 0x01) << 2) | (btn & 0x02) | ((btn & 0x04) >> 2); // S3, S0, S1, S2
    int learn          = (b[7] & 0x0f) == 0x0f;
    int battery_low    = (b[8] & 0x80) == 0x80;
    int repeat         = (b[8] & 0x40) == 0x40;

    char encrypted_str[9];
    snprintf(encrypted_str, sizeof(encrypted_str), "%08X", encrypted);
    char serial_str[9];
    snprintf(serial_str, sizeof(serial_str), "%07X", serial);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING,    "Microchip-HCS362",
            "id",               "",             DATA_STRING,    serial_str,
            "battery_ok",       "Battery",      DATA_INT,       !battery_low,
            "button",           "Button",       DATA_INT,       btn_num,
            "learn",            "Learn mode",   DATA_INT,       learn,
            "repeat",           "Repeat",       DATA_INT,       repeat,
            "encrypted",        "",             DATA_STRING,    encrypted_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "button",
        "learn",
        "repeat",
        "encrypted",
        NULL,
};

r_device const hcs362_pwm = {
        .name        = "Microchip HCS362 KeeLoq Hopping Encoder based remotes (mode 0)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 200, // 100 us = 3333 bps, 200 us = 1667 bps, 400 us = 833 bps, 800 us = 417 bps
        .long_width  = 400,
        .gap_limit   = 600,
        .reset_limit = 900,
        .tolerance   = 50, // us
        .decode_fn   = &hcs362_decode,
        .fields      = output_fields,
};

r_device const hcs362_mc = {
        .name        = "Microchip HCS362 KeeLoq Hopping Encoder based remotes (mode 1)",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 200, // 100 us = 5000 bps, 200 us = 2500 bps, 400 us = 1250 bps, 800 us = 625 bps
        .long_width  = 200,
        .gap_limit   = 600,
        .reset_limit = 900,
        .tolerance   = 50, // us
        .decode_fn   = &hcs362_decode,
        .fields      = output_fields,
};
