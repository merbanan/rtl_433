/** @file
    Security+ 2.0 rolling code.

    Copyright (C) 2020 Peter Shipley <peter.shipley@gmail.com>
    Based on code by Clayton Smith https://github.com/argilo/secplus

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Freq 310, 315 and 390 MHz.

Security+ 2.0  is described in [US patent application US20110317835A1](https://patents.google.com/patent/US20110317835A1/)


*/

#include "decoder.h"

/**
Security+ 2.0 rolling code.

Data comes in two bursts/packets.

Layout:

    bits = `AA BB IIII OOOO X*30`

- AA = payload type  (2 bits 00 or 01)
- BB = FrameID (2 bits always 00)
- IIII = inversion indicator (4 bits)
- OOOO = Order indicator (4 bits).
- XXXX....  = data (30 bits)

---

data is broken up into 3 parts (p0 p1 p2)
eg:

data = `ABCABCABCABCABCABCABCABCABCABC`
becomes:

    `p0 = AAAAAAAAAA`
    `p1 = BBBBBBBBBB`
    `p2 = CCCCCCCCCC`

these three parts are then inverted and reordered based on the 4bit Order and Inversion indicators

fixed generated from concatenate  p0 + p1

roll_array is generated from the 8 bit used for Order and Inversion indicators + p3
by reading the buffer in binary bit pairs forming trinary values

EG:
`1 0 0 1 1 0 1 0 0 1 1 0=> [1 0] [0 1] [1 0] [1 0] [0 1] [1 0] => 2 1 2 2 1 2`

Returns data in :
  * roll_array as an array of trinary values  0, 1, 2) the value 3 is invalid
  * fixed_p as an bitbuffer_t with 20 bits of data


Once the above has been run twice the two are merged

---

*/

static int secplus_v2_decode_v2_half(r_device *decoder, bitbuffer_t *bits, uint8_t roll_array[], bitbuffer_t *fixed_p)
{
    uint8_t invert = 0;
    uint8_t order  = 0;
    uint32_t x    = 0;
    unsigned int start_pos = 2; //
    uint8_t buffy[10];

    uint8_t part_id = (bits->bb[0][0] >> 6);

    decoder_log_bitrow(decoder, 1, __func__, bits->bb[0], bits->bits_per_row[0], "");

    bitbuffer_extract_bytes(bits, 0, start_pos, buffy, 2);
    start_pos += 2;

    bitbuffer_extract_bytes(bits, 0, start_pos, buffy, 8);
    start_pos += 8;
    order = buffy[0] >> 4;

    invert = buffy[0] & 0x0f;
    // bitrow_debug(&invert, 8);

    bitbuffer_extract_bytes(bits, 0, start_pos, buffy, 30);
    start_pos += 30;

    // copy 30 bits of data into 32bit int then shift >> 2
    // memcpy(&dat, buffy, 4);
    x = ((uint32_t)buffy[0] << 24) | (buffy[1] << 16) | (buffy[2] << 8) | (buffy[3]);

    x >>= 2;

    // using short to store 10bit values
    uint16_t p0 = 0, p1 = 0, p2 = 0;

    // sort 30 bits of interleaved data into three 10 bit buffers
    for (int i = 0; i < 10; i++) {
        p2 ^= (x & 0x00000001) << i; // 9-
        x >>= 1;
        p1 ^= (x & 0x00000001) << i;
        x >>= 1;
        p0 ^= (x & 0x00000001) << i;
        x >>= 1;
    }

    // selectively invert buffers
    switch (invert) {
    case 0x00: // 0b0000 (True, True, False),
        p0 = ~p0 & 0x03FF;
        p1 = ~p1 & 0x03FF;
        break;
    case 0x01: // 0b0001 (False, True, False),
        p1 = ~p1 & 0x03FF;
        break;
    case 0x02: // 0b0010 (False, False, True),
        p2 = ~p2 & 0x03FF;
        break;
    case 0x04: // 0b0100 (True, True, True),
        p0 = ~p0 & 0x03FF;
        p1 = ~p1 & 0x03FF;
        p2 = ~p2 & 0x03FF;
        break;
    case 0x05: // 0b0101 (True, False, True),
    case 0x0a: // 0b1010 (True, False, True),
        p0 = ~p0 & 0x03FF;
        p2 = ~p2 & 0x03FF;
        break;
    case 0x06: // 0b0110 (False, True, True),
        p1 = ~p1 & 0x03FF;
        p2 = ~p2 & 0x03FF;
        break;
    case 0x08: // 0b1000 (True, False, False),
        p0 = ~p0 & 0x03FF;
        break;
    case 0x09: // 0b1001 (False, False, False),
        break;
    default:
        decoder_log(decoder, 1, __func__, "Invert FAIL");
        return 1;
    }

    uint16_t a = p0, b = p1, c = p2;

    // selectively reorder buffers
    switch (order) {
    case 0x06: // 0b0110  2, 1, 0],
    case 0x09: // 0b1001  2, 1, 0],
        p2 = a;
        p1 = b;
        p0 = c;
        break;

    case 0x08: // 0b1000  1, 2, 0],
    case 0x04: // 0b0100  1, 2, 0],
        p1 = a;
        p2 = b;
        p0 = c;
        break;

    case 0x01: // 0b0001 2, 0, 1],
        p2 = a;
        p0 = b;
        p1 = c;
        break;

    case 0x00: // 0b0000  0, 2, 1],
        p0 = a;
        p2 = b;
        p1 = c;
        break;

    case 0x05: // 0b0101 1, 0, 2],
        p1 = a;
        p0 = b;
        p2 = c;
        break;

    case 0x02: // 0b0010 0, 1, 2],
    case 0x0A: // 0b1010 0, 1, 2],
        p0 = a;
        p1 = b;
        p2 = c;
        break;

    default:
        decoder_log(decoder, 1, __func__, "Order FAIL");
        return 2;
    }

    bitbuffer_extract_bytes(bits, 0, 4, buffy, 8);
    x     = buffy[0];
    int k = 0;
    for (int i = 6; i >= 0; i -= 2) {
        roll_array[k++] = (x >> i) & 0x03;
    }

    // decoder_log_bitrow(decoder, 3, __func__, buffy, 8, "")

    // assemble binary bits into trinary
    x = p2;
    for (int i = 8; i >= 0; i -= 2) {
        roll_array[k++] = (x >> i) & 0x03;
    }

    decoder_logf(decoder, 1, __func__, "roll_array : (%d) %d %d %d %d %d %d %d %d %d", part_id,
                roll_array[0], roll_array[1], roll_array[2], roll_array[3],
                roll_array[4], roll_array[5], roll_array[6], roll_array[7], roll_array[8]);

    // SANITY check trinary values, 00/01/10 are valid,  11 is not
    for (int i = 0; i < 9; i++) {
        if (roll_array[i] == 3) {
            decoder_log(decoder, 0, __func__, "roll_array val FAIL");
            return 1; // DECODE_FAIL_SANITY;
        }
    }

    // fixed_p = p0 + p1
    for (int i = 9; i >= 0; i--) {
        bitbuffer_add_bit(fixed_p, (p0 >> i) & 0x01);
    }
    for (int i = 9; i >= 0; i--) {
        bitbuffer_add_bit(fixed_p, (p1 >> i) & 0x01);
    }

    return 0;
}

static const uint8_t _preamble[] = {0xaa, 0xaa, 0x95, 0x60};
unsigned _preamble_len           = 28;

/**
Security+ 2.0 rolling code.
@sa secplus_v2_decode_v2_half()
*/
static int secplus_v2_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    unsigned search_index = 0;
    bitbuffer_t bits = {0};
    // int i            = 0;

    //bitbuffer_t bits_1    = {0};
    bitbuffer_t fixed_1   = {0};
    uint8_t rolling_1[16] = {0};

    //bitbuffer_t bits_2    = {0};
    bitbuffer_t fixed_2   = {0};
    uint8_t rolling_2[16] = {0};

    for (uint16_t row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] < 110) {
            continue;
        }

        search_index = bitbuffer_search(bitbuffer, row, 0, _preamble, _preamble_len);

        if (search_index >= bitbuffer->bits_per_row[row]) {
            break;
        }

        bitbuffer_clear(&bits);
        bitbuffer_manchester_decode(bitbuffer, row, search_index + 26, &bits, 80);
        search_index += 20;
        if (bits.bits_per_row[0] < 42) {
            continue; // DECODE_ABORT_LENGTH;
        }

        decoder_log_bitrow(decoder, 1, __func__, bits.bb[0], bits.bits_per_row[0], "manchester decoded");

        // valid = 0X00XXXX
        // 1st 3rs and 4th bits should always be 0
        if (bits.bb[0][0] & 0xB0) {
            continue; // DECODE_FAIL_SANITY;
        }

        // 2nd bit indicates with half of the data
        if (bits.bb[0][0] & 0xC0) {
            decoder_log(decoder, 1, __func__, "Set 2");
            secplus_v2_decode_v2_half(decoder, &bits, rolling_2, &fixed_2);
        }
        else {
            decoder_log(decoder, 1, __func__, "Set 1");
            secplus_v2_decode_v2_half(decoder, &bits, rolling_1, &fixed_1);
        }

        // break if we've received both halves
        if (fixed_1.bits_per_row[0] > 1 && fixed_2.bits_per_row[0] > 1) {
            break;
        }
    }

    // Do we have what we need ??
    if (fixed_1.bits_per_row[0] == 0 || fixed_2.bits_per_row[0] == 0) {
        return DECODE_FAIL_SANITY;
    }

    // Assemble rolling_1[] and rolling_2[] into rolling_digits[]
    uint8_t rolling_digits[24] = {0};
    uint8_t *r;

    r    = rolling_digits;
    *r++ = rolling_2[8];
    *r++ = rolling_1[8];
    for (int i = 4; i < 8; i++) {
        *r++ = rolling_2[i];
    }
    for (int i = 4; i < 8; i++) {
        *r++ = rolling_1[i];
    }

    for (int i = 0; i < 4; i++) {
        *r++ = rolling_2[i];
    }
    for (int i = 0; i < 4; i++) {
        *r++ = rolling_1[i];
    }

    // compute rolling_total from rolling_digits[]
    uint32_t rolling_total = 0;
    uint32_t rolling_temp  = 0;

    for (int i = 0; i < 18; i++) {
        rolling_temp = (rolling_temp * 3) + rolling_digits[i];
    }

    // Max value = 2^28 (268435456)
    if (rolling_temp >= 0x10000000) {
        return DECODE_FAIL_SANITY;
    }

    // value is 28 bits thus need to shift over 4 bit
    rolling_total = reverse32(rolling_temp);
    rolling_total = rolling_total >> 4;

    // Assemble "fixed" data part
    uint64_t fixed_total = 0;
    uint8_t *bb;
    bb = fixed_1.bb[0];
    fixed_total ^= ((uint64_t)bb[0]) << 32;
    fixed_total ^= ((uint64_t)bb[1]) << 24;
    fixed_total ^= ((uint64_t)bb[2]) << 16;

    bb = fixed_2.bb[0];
    fixed_total ^= ((uint64_t)bb[0]) << 12;
    fixed_total ^= ((uint64_t)bb[1]) << 4;
    fixed_total ^= (bb[2] >> 4) & 0x0f;

    // int button    = fixed_total >> 32;
    // int remote_id = fixed_total & 0xffffffff;
    char fixed_str[16];
    char rolling_str[16];

    // rolling_total is a 28 bit unsigned number
    // fixed_totals is 40 bit in a uint64_t
    snprintf(fixed_str, sizeof(fixed_str), "%llu", (long long unsigned)fixed_total);
    snprintf(rolling_str, sizeof(rolling_str), "%u", rolling_total);

    /* clang-format off */
    data_t *data;
    data = data_make(
            "model",       "Model",    DATA_STRING, "Secplus-v2",
            "id",          "",       DATA_INT, (fixed_total & 0xffffffff),
            "button_id",   "Button-ID",    DATA_INT,    (fixed_total >> 32),
            "remote_id",   "Remote-ID",    DATA_INT,    (fixed_total & 0xffffffff),
            // "fixed",       "",    DATA_INT,    fixed_total,
            "fixed",       "Fixed_Code",    DATA_STRING,    fixed_str,
            "rolling",     "Rolling_Code",    DATA_STRING,    rolling_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        // Common fields
        "model",
        "id",
        "rolling",
        "fixed",
        "button_id",
        "remote_id",
        NULL,
};

//      Freq 310.01M
//  -X "n=vI3,m=OOK_PCM,s=230,l=230,t=40,r=10000,g=7400,match={24}0xaaaa9560"

r_device const secplus_v2 = {
        .name        = "Security+ 2.0 (Keyfob)",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 250,
        .long_width  = 250,
        .tolerance   = 50,
        .gap_limit   = 1500,
        .reset_limit = 9000,
        .decode_fn   = &secplus_v2_callback,
        .fields      = output_fields,
};
