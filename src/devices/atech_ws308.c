/** @file
    Decoder for Atech-WS308 temperature sensor.

    Copyright (C) 2020 Marc Prieur https://github.com/marco402
    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int atech_ws308_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Atech WS-308 "433 tech remote sensor" for Atech wireless weather station.

S.a. #1605

Coding:

- 28 bit, PWM encoded as PCM RZ 1600us/1832us
- PCM-RZ to PWM coding: 10->0, 1110->1

Example:

    rtl_433 -R 0 -X 'n=name,m=OOK_PCM,s=1600,l=1800,g=2500,r=9000' Atech-433/g001_433.92M_250k.cu8
    {9}ff0, {71}aaeeaaaabbaabaaaec

    111111110000
    10 10 10 10 1110 1110 10 10 10 10 10 10 10 10 10 1110 1110 10 10 10 10 1110 10 10 10 10 10 1110 110
     0  0  0  0   1    1   0  0  0  0  0  0  0  0  0   1    1   0  0  0  0   1   0  0  0  0  0   1   x
    y 0000 1100 0000 0001 1000 0100 0001 x
    x 0 c 0 1 8 4 1 ; 18.4 C, XOR=0

Data layout:

- nibble 0: sync or id, b0000
- nibble 1: sync or id, b1100
- nibble 2: Temperature sign, 3rd bit: ??S?
- nibble 3: Temperature BCD hundreds
- nibble 4: Temperature BCD tenths
- nibble 5: Temperature BCD units
- nibble 6: checksum XOR even parity of all nibbles

*/

static unsigned pwm_decode(uint8_t *bits, unsigned bit_len, uint8_t *out, unsigned out_len)
{
    unsigned pos = 0;
    unsigned cnt = 0;
    for (unsigned i = 0; i < bit_len; ++i) {
        if (bits[i / 8] & (1 << (7 - (i % 8)))) {
            // count 1's
            cnt++;
        }
        else {
            // decide at 0
            // 10->0, 1110->1, otherwise error
            if (pos % 8 == 0)
                out[pos / 8] = 0;
            if (cnt == 1) {
                // add a zero
                pos++;
            }
            else if (cnt == 3) {
                // add a one
                out[pos / 8] |= (1 << (7 - (pos % 8)));
                pos++;
            }
            else {
                break;
            }
            if (pos >= out_len)
                break;
            cnt = 0;
        }
    }
    return pos;
}

static int atech_ws308_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 2)
        return DECODE_ABORT_EARLY;
    if (bitbuffer->bits_per_row[1] < 58)
        return DECODE_ABORT_LENGTH;

    uint8_t b[4]; // 28 bit
    int len = pwm_decode(bitbuffer->bb[1], bitbuffer->bits_per_row[1], b, 32);
    //decoder_log_bitrow(decoder, 0, __func__, b, len, "");
    if (len < 28)
        return DECODE_ABORT_LENGTH;

    //if (b[0] != 0x0c)
    //    return DECODE_FAIL_SANITY;

    // check even nibble parity
    int chk = xor_bytes(b, 3);
    chk = ((chk ^ b[3]) >> 4) ^ (chk & 0xf); // fold nibbles
    if (chk != 0)
        return DECODE_FAIL_MIC;

    int id       = b[0]; // actually fixed 0x0c
    int temp_raw = ((b[1] & 0xf) * 100) + ((b[2] >> 4) * 10) + (b[2] & 0xf);
    int sign     = (b[1] & 0x20) ? -1 : 1;
    float temp_c = sign * temp_raw * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Atech-WS308",
            "id",               "Fixed ID",     DATA_INT,    id,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "mic",              "Integrity",    DATA_STRING, "PARITY",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "mic",
        NULL,
};

r_device const atech_ws308 = {
        .name        = "Atech-WS308 temperature sensor",
        .modulation  = OOK_PULSE_RZ,
        .short_width = 1600,
        .long_width  = 1832,
        .gap_limit   = 2500,
        .reset_limit = 9000,
        .decode_fn   = &atech_ws308_decode,
        .fields      = output_fields,
};
