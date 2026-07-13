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
Compute the 2-bit CRC per datasheet Equation 3-1, over the 65 bits
(encrypted portion + fixed portion + battery low bit) preceding the CRC
bits themselves. Returns the computed (CRC1 << 1 | CRC0) value.
*/
static int hcs362_crc(uint8_t const *b)
{
    int crc0 = 0;
    int crc1 = 0;
    for (int n = 0; n < 65; n++) {
        int d         = (b[n / 8] >> (7 - (n % 8))) & 1;
        int next_crc1 = crc0 ^ d;
        int next_crc0 = crc0 ^ d ^ crc1;
        crc0          = next_crc0;
        crc1          = next_crc1;
    }
    return crc1 << 1 | crc0;
}

/**
Microchip HCS362 KeeLoq Code Hopping Encoder based remotes.

There are two transmission modes: PWM (mode 0) and MC (mode 1).

With Extended Serial Number disabled (XSER = 0) and CRC status selected
(CTSEL = 1), 69 bits are transmitted, LSB first:

|  0-31 | 32 bit Encrypted Portion (hopping code, needs the crypt key to decode)
| 32-59 | 28 bit Serial Number
| 60-63 |  4 bit Function Code (S3, S0, S1, S2)
| 64    |  1 bit Battery Low (Low Voltage Detector Status, VLOW)
| 65-66 |  2 bit CRC (CRC0, CRC1)
| 67-68 |  2 bit Queue (QUEUE0, QUEUE1)

Note that the button bits are (MSB/first sent to LSB) S3, S0, S1, S2.
Hardware buttons might map to combinations of these bits.

The CRC is computed over the preceding 65 bits (Encrypted Portion, Serial
Number, Function Code and VLOW) per datasheet Equation 3-1, a 2-bit shift
register seeded to 0 and updated per transmitted bit Di (0 <= i <= 64):

    CRC1[i+1] = CRC0[i] ^ Di
    CRC0[i+1] = CRC0[i] ^ Di ^ CRC1[i]

This decoder assumes the encoder is configured for CRC status (CTSEL = 1,
the default); if the encoder is configured for TIME bits (CTSEL = 0)
instead, the CRC check below will always fail.

- Datasheet HCS362: https://ww1.microchip.com/downloads/aemDocuments/documents/MCU08/ProductDocuments/DataSheets/40189E.pdf

PWM mode:

The preamble is 12 short pulses (0xfff), followed by the 69 data bits, PWM coded
(short is 1, long is 0).

    rtl_433 -R 0 -X 'n=HCS362,m=OOK_PWM,s=200,l=400,g=550,r=900'

MC mode:

The preamble is 12 short pulses, followed by a long sync gap, then a hardcoded
start bit (logic 1) and 69 MC coded data bits, then a stop bit that
breaks the MC coding and ends the transmission.

    rtl_433 -R 0 -X 'n=HCS362,m=OOK_PCM,s=214,l=214,g=600,r=900'

Based on the sketch from https://github.com/merbanan/rtl_433/pull/3113,
decoding was corrected and confirmed working (both PWM and MC,
including the CRC) against real hardware captures from
https://github.com/merbanan/rtl_433/issues/3112.
*/

static int hcs362_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b;
    bitbuffer_t msg = {0};

    if (decoder->modulation == OOK_PULSE_PCM) {
        // Check preamble
        if (bitbuffer->bits_per_row[0] < 12 * 2 - 8 || bitbuffer->bits_per_row[0] > 12 * 2 + 8) {
            decoder_log(decoder, 2, __func__, "Preamble not found");
            return DECODE_ABORT_LENGTH;
        }
        // Reject codes with an incorrect preamble (expected 0xaaaaaa)
        b = bitbuffer->bb[0];
        if (b[0] != 0xaa || b[1] != 0xaa || b[2] != 0xaa) {
            decoder_log(decoder, 2, __func__, "Preamble invalid");
            return DECODE_ABORT_EARLY;
        }
        // Reject codes of wrong length (71 data+start/stop bits, MC coded,
        // the trailing stop bit is usually truncated to a single raw bit)
        if (bitbuffer->bits_per_row[1] < 71 * 2 || bitbuffer->bits_per_row[1] > 72 * 2 + 4) {
            decoder_log(decoder, 2, __func__, "Data length invalid");
            return DECODE_ABORT_LENGTH;
        }

        // Second row is data
        b = bitbuffer->bb[1];
        // Check for the start bit
        if ((b[0] & 0xc0) != 0x80) {
            decoder_log(decoder, 2, __func__, "Startbit not found");
            return DECODE_ABORT_EARLY;
        }
        // MC decode, excluding the start bit
        unsigned len = bitbuffer_manchester_decode(bitbuffer, 1, 2, &msg, 72);
        decoder_log_bitbuffer(decoder, 1, __func__, &msg, "Decoded");

        // Reject codes of wrong length
        if (len < 69 + 1) {
            return DECODE_ABORT_LENGTH;
        }

        bitbuffer_invert(&msg); // want G.E.Thomas, not IEEE 802.3
        b = msg.bb[0];
    }
    else {
        // Reject codes of wrong length
        if (bitbuffer->bits_per_row[0] != 12 || bitbuffer->bits_per_row[1] != 69) {
            return DECODE_ABORT_LENGTH;
        }

        // Reject codes with an incorrect preamble (expected 0xfff)
        b = bitbuffer->bb[0];
        if (b[0] != 0xff || (b[1] & 0xf0) != 0xf0) {
            decoder_log(decoder, 2, __func__, "Preamble not found");
            return DECODE_ABORT_EARLY;
        }

        // Second row is data
        b = bitbuffer->bb[1];
    }

    // No need to decode/extract values for simple test
    if (b[1] == 0xff && b[2] == 0xff && b[3] == 0xff && b[4] == 0xff
            && b[5] == 0xff && b[6] == 0xff && b[7] == 0xff) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0xff");
        return DECODE_FAIL_SANITY;
    }

    // Status byte: bit64 VLOW, bit65 CRC0, bit66 CRC1, bit67 QUEUE0, bit68 QUEUE1
    int actual_crc = ((b[8] >> 6) & 1) | (((b[8] >> 5) & 1) << 1);
    if (actual_crc != hcs362_crc(b)) {
        decoder_log(decoder, 2, __func__, "CRC invalid");
        return DECODE_FAIL_MIC;
    }

    // The transmission is LSB first, big endian.
    uint32_t encrypted = (uint32_t)reverse8(b[3]) << 24 | (uint32_t)reverse8(b[2]) << 16 | (uint32_t)reverse8(b[1]) << 8 | reverse8(b[0]);
    uint32_t serial     = (uint32_t)reverse8(b[7] & 0xf0) << 24 | (uint32_t)reverse8(b[6]) << 16 | (uint32_t)reverse8(b[5]) << 8 | reverse8(b[4]);
    int btn             = (b[7] & 0x0f);
    int btn_num         = (btn & 0x08) | ((btn & 0x01) << 2) | (btn & 0x02) | ((btn & 0x04) >> 2); // S3, S0, S1, S2
    int battery_low     = (b[8] & 0x80) == 0x80;
    int queue           = ((b[8] >> 4) & 1) | (((b[8] >> 3) & 1) << 1);

    char encrypted_str[9];
    snprintf(encrypted_str, sizeof(encrypted_str), "%08X", encrypted);
    char serial_str[8];
    snprintf(serial_str, sizeof(serial_str), "%07X", serial);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING,    "Microchip-HCS362",
            "id",               "",             DATA_STRING,    serial_str,
            "battery_ok",       "Battery",      DATA_INT,       !battery_low,
            "button",           "Button",       DATA_INT,       btn_num,
            "repeat",           "Repeat",       DATA_INT,       queue,
            "encrypted",        "",             DATA_STRING,    encrypted_str,
            "mic",              "Integrity",    DATA_STRING,    "CRC",
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
        "repeat",
        "encrypted",
        "mic",
        NULL,
};

r_device const hcs362_pwm = {
        .name        = "Microchip HCS362 KeeLoq PWM",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 200, // 1x TE
        .long_width  = 400, // 2x TE
        .gap_limit   = 550,
        .reset_limit = 900,
        .tolerance   = 50,
        .decode_fn   = &hcs362_decode,
        .fields      = output_fields,
};

r_device const hcs362_mc = {
        .name        = "Microchip HCS362 KeeLoq MC",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 214,
        .long_width  = 214,
        .gap_limit   = 600,
        .reset_limit = 900,
        .tolerance   = 50,
        .decode_fn   = &hcs362_decode,
        .fields      = output_fields,
};
