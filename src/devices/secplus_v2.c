/** @file
    Security+ 2.0 rolling code.

    Copyright (C) 2020 Peter Shipley <peter.shipley@gmail.com>
    Copyright (C) 2022 Clayton Smith <argilo@gmail.com>
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
#include "compat_time.h"

/**
Security+ 2.0 rolling code.

Data comes in two bursts/packets.

Layout:

    bits = `AA BB IIII OOOO XXXX....`

- AA = Frame ID (2 bits 00 or 01)
- BB = Frame type (2 bits 00 or 01)
- IIII = inversion indicator (4 bits)
- OOOO = Order indicator (4 bits)
- XXXX....  = data (30 or 54 bits)

---

data is broken up into 3 parts (p0 p1 p2)
eg:

data = `ABCABCABCABCABCABCABCABCABCABC(ABCABCABCABCABCABCABCABC)`
becomes:

    `p0 = AAAAAAAAAA(AAAAAAAA)`
    `p1 = BBBBBBBBBB(BBBBBBBB)`
    `p2 = CCCCCCCCCC(CCCCCCCC)`

these three parts are then inverted and reordered based on the 4bit Order and Inversion indicators

fixed generated from concatenating first 10 bits of p0 & p1
(optional) data generated from concatenating last 8 bits of p0 & p1

roll_array is generated from the 8 bit used for Order and Inversion indicators + p3
by reading the buffer in binary bit pairs forming trinary values

EG:
`1 0 0 1 1 0 1 0 0 1 1 0=> [1 0] [0 1] [1 0] [1 0] [0 1] [1 0] => 2 1 2 2 1 2`

Returns data in :
  * roll_array as an array of trinary values (0, 1, 2) the value 3 is invalid
  * fixed_p as an bitbuffer_t with 20 bits of data


Once the above has been run twice the two are merged

---

*/

static int8_t v2_check_parity(const uint64_t fixed, const uint32_t data)
{
    uint32_t parity = (fixed >> 32) & 0xf;
    int8_t offset;

    for (offset = 0; offset < 32; offset += 4) {
        parity ^= ((data >> offset) & 0xf);
    }

    if (parity != 0) {
        return -1;
    }

    return 0;
}

static int8_t decode_v2_rolling(const uint32_t *rolling_halves, uint32_t *rolling)
{
    int8_t i, half;
    uint32_t rolling_reversed;

    rolling_reversed = (rolling_halves[1] >> 8) & 3;
    rolling_reversed = (rolling_reversed * 3) + ((rolling_halves[0] >> 8) & 3);

    for (half = 1; half >= 0; half--) {
        for (i = 16; i >= 10; i -= 2) {
            rolling_reversed = (rolling_reversed * 3) + ((rolling_halves[half] >> i) & 3);
        }
    }

    for (half = 1; half >= 0; half--) {
        for (i = 6; i >= 0; i -= 2) {
            rolling_reversed = (rolling_reversed * 3) + ((rolling_halves[half] >> i) & 3);
        }
    }

    if (rolling_reversed >= 0x10000000) {
        return -1;
    }

    *rolling = 0;
    for (i = 0; i < 28; i++) {
        *rolling |= ((rolling_reversed >> i) & 1) << (28 - i - 1);
    }

    return 0;
}

static int8_t v2_combine_halves(const uint8_t frame_type,
        const uint32_t *rolling_halves, const uint32_t *fixed_halves, const uint16_t *data_halves,
        uint32_t *rolling, uint64_t *fixed, uint32_t *data)
{
    int8_t err = 0;

    err = decode_v2_rolling(rolling_halves, rolling);
    if (err < 0) {
        return err;
    }

    *fixed = ((uint64_t)fixed_halves[0] << 20) | fixed_halves[1];

    if (frame_type == 1) {
        *data = ((uint32_t)data_halves[0] << 16) | data_halves[1];

        err = v2_check_parity(*fixed, *data);
        if (err < 0) {
            return err;
        }
    }

    return 0;
}

static const int8_t ORDER[16]  = {9, 33, 6, -1, 24, 18, 36, -1, 24, 36, 6, -1, -1, -1, -1, -1};
static const int8_t INVERT[16] = {6, 2, 1, -1, 7, 5, 3, -1, 4, 0, 5, -1, -1, -1, -1, -1};

static int8_t v2_unscramble(const uint8_t frame_type, const uint8_t indicator,
        const uint8_t *packet_half, uint32_t *parts)
{
    const int8_t order  = ORDER[indicator >> 4];
    const int8_t invert = INVERT[indicator & 0xf];
    int8_t i;
    uint8_t out_offset         = 10;
    const int8_t end           = (frame_type == 0 ? 8 : 0);
    uint32_t parts_permuted[3] = {0, 0, 0};

    if ((order == -1) || (invert == -1)) {
        return -1;
    }

    for (i = 18 - 1; i >= end; i--) {
        parts_permuted[0] |= (uint32_t)((packet_half[out_offset >> 3] >> (7 - (out_offset % 8))) & 1) << i;
        out_offset++;
        parts_permuted[1] |= (uint32_t)((packet_half[out_offset >> 3] >> (7 - (out_offset % 8))) & 1) << i;
        out_offset++;
        parts_permuted[2] |= (uint32_t)((packet_half[out_offset >> 3] >> (7 - (out_offset % 8))) & 1) << i;
        out_offset++;
    }

    parts[(order >> 4) & 3] = (invert & 4) ? ~parts_permuted[0] : parts_permuted[0];
    parts[(order >> 2) & 3] = (invert & 2) ? ~parts_permuted[1] : parts_permuted[1];
    parts[order & 3]        = (invert & 1) ? ~parts_permuted[2] : parts_permuted[2];

    return 0;
}

static int8_t decode_v2_half_parts(const uint8_t frame_type, const uint8_t indicator,
        const uint8_t *packet_half, uint32_t *rolling, uint32_t *fixed, uint16_t *data)
{
    int8_t err = 0;
    int8_t i;
    uint32_t parts[3];

    err = v2_unscramble(frame_type, indicator, packet_half, parts);
    if (err < 0) {
        return err;
    }

    if ((frame_type == 1) && ((parts[2] & 0xff) != indicator)) {
        return -1;
    }

    for (i = 8; i < 18; i += 2) {
        if (((parts[2] >> i) & 3) == 3) {
            return -1;
        }
    }

    *rolling = (parts[2] & 0x3ff00) | indicator;
    *fixed   = ((parts[0] & 0x3ff00) << 2) | ((parts[1] & 0x3ff00) >> 8);
    *data    = ((parts[0] & 0xff) << 8) | (parts[1] & 0xff);

    return 0;
}

static int8_t decode_v2_half(const uint8_t frame_type, const uint8_t *packet_half,
        uint32_t *rolling, uint32_t *fixed, uint16_t *data)
{
    int8_t err              = 0;
    const uint8_t indicator = (packet_half[0] << 2) | (packet_half[1] >> 6);

    if ((packet_half[0] >> 6) != frame_type) {
        return -1;
    }

    err = decode_v2_half_parts(frame_type, indicator, packet_half, rolling, fixed, data);
    if (err < 0) {
        return err;
    }

    return 0;
}

static int8_t decode_v2(uint8_t frame_type, const uint8_t *packet1, const uint8_t *packet2,
        uint32_t *rolling, uint64_t *fixed, uint32_t *data)
{
    int8_t err = 0;
    uint32_t rolling_halves[2];
    uint32_t fixed_halves[2];
    uint16_t data_halves[2];

    err = decode_v2_half(frame_type, packet1, &rolling_halves[0], &fixed_halves[0], &data_halves[0]);
    if (err < 0) {
        return err;
    }

    err = decode_v2_half(frame_type, packet2, &rolling_halves[1], &fixed_halves[1], &data_halves[1]);
    if (err < 0) {
        return err;
    }

    err = v2_combine_halves(frame_type, rolling_halves, fixed_halves, data_halves, rolling, fixed, data);
    if (err < 0) {
        return err;
    }

    return 0;
}

static const uint8_t _preamble[] = {0xaa, 0xaa, 0x95, 0x60};
unsigned _preamble_len           = 28;

// max age for cache in us
#define MAX_TIME_DIFF 800000

static uint8_t packet[2][8]        = {0};
static struct timeval packet_tv[2] = {0};

/**
Security+ 2.0 rolling code.
*/
static int secplus_v2_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    unsigned search_index = 0;
    bitbuffer_t bits      = {0};

    if (bitbuffer->bits_per_row[0] < 110) {
        return DECODE_ABORT_LENGTH;
    }

    search_index = bitbuffer_search(bitbuffer, 0, 0, _preamble, _preamble_len);

    if (search_index >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_clear(&bits);
    bitbuffer_manchester_decode(bitbuffer, 0, search_index + 14, &bits, 72);
    if (bits.bits_per_row[0] < 42) {
        return DECODE_ABORT_LENGTH;
    }

    decoder_log_bitrow(decoder, 1, __func__, bits.bb[0], bits.bits_per_row[0], "manchester decoded");

    uint8_t frame_id = bits.bb[0][0] & 3;
    if (frame_id > 1) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t frame_type = (bits.bb[0][1] >> 6) & 3;
    if (frame_type > 1) {
        return DECODE_ABORT_EARLY;
    }

    int frame_len = (frame_type == 0) ? 40 : 64;
    if (bits.bits_per_row[0] < 2 + frame_len) {
        return DECODE_ABORT_LENGTH;
    }

    gettimeofday(&packet_tv[frame_id], NULL);

    if (memcmp(packet[frame_id], &bits.bb[0][1], frame_len / 8) == 0) {
        return DECODE_ABORT_EARLY;
    }

    memcpy(packet[frame_id], &bits.bb[0][1], frame_len / 8);

    if (packet_tv[frame_id ^ 1].tv_sec && ((packet[frame_id ^ 1][0] >> 6) & 3) == frame_type) {
        struct timeval res_tv;
        timeval_subtract(&res_tv, &packet_tv[frame_id], &packet_tv[frame_id ^ 1]);
        if (res_tv.tv_sec == 0 && res_tv.tv_usec < MAX_TIME_DIFF) {
            uint32_t rolling      = 0;
            uint64_t fixed        = 0;
            uint32_t secplus_data = 0;
            int8_t ret            = decode_v2(frame_type, packet[0], packet[1], &rolling, &fixed, &secplus_data);
            if (ret < 0) {
                return DECODE_FAIL_SANITY;
            }

            int button = (fixed >> 32) & 0xf;
            // int remote_id = fixed & 0xffffffff;
            char rolling_str[9];
            char fixed_str[17];
            char remote_id_str[17];
            char data_str[9] = "";
            char pin_str[7]  = "";

            // rolling_total is a 28 bit unsigned number
            // fixed_totals is 40 bit in a uint64_t
            snprintf(rolling_str, sizeof(rolling_str), "%07x", rolling);
            snprintf(fixed_str, sizeof(fixed_str), "%010llx", (long long unsigned)fixed);
            snprintf(remote_id_str, sizeof(fixed_str), "%010llx", (long long unsigned)fixed & 0xf0ffffffff);
            if (frame_type == 1) {
                snprintf(data_str, sizeof(rolling_str), "%08x", secplus_data);
                int pin = (((secplus_data >> 16) & 0xff) << 8) | (secplus_data >> 24);
                if (button == 3) {
                    strcat(pin_str, "enter");
                }
                else {
                    snprintf(pin_str, sizeof(pin_str), "%04d", pin);
                    if (button == 1) {
                        strcat(pin_str, "*");
                    }
                    else if (button == 2) {
                        strcat(pin_str, "#");
                    }
                }
            }

            /* clang-format off */
            data_t *data;
            data = data_make(
                    "model",     "Model",        DATA_STRING, "Secplus-v2",
                    "id",        "",             DATA_INT,    (fixed & 0xffffffff),
                    "button_id", "Button-ID",    DATA_INT,    button,
                    "remote_id", "Remote-ID",    DATA_STRING, remote_id_str,
                    "rolling",   "Rolling_Code", DATA_STRING, rolling_str,
                    "fixed",     "Fixed_Code",   DATA_STRING, fixed_str,
                    "data",      "Data",         DATA_STRING, data_str,
                    "pin",       "PIN",          DATA_STRING, pin_str,
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
        }
    }

    return 1;
}

static char const *const output_fields[] = {
        // Common fields
        "model",
        "id",
        "rolling",
        "fixed",
        "data",
        "button_id",
        "remote_id",
        "pin",
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
        .reset_limit = 1500,
        .decode_fn   = &secplus_v2_callback,
        .fields      = output_fields,
};
