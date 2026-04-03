/** @file
    Thermor A6N 132TX temperature sensor.

    Copyright (C) 2020 Jon Klixbuell Langeland
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Thermor A6N 132TX temperature sensor.

FCC: https://fccid.io/A6N-132TX

32-bit frame, repeated 11 times (look for 5 identical versions).

Data layout:

    IIIICC-- TTTTTTTT TTTTTTTT CCCCCCCC

- I: 4 bit ID
- C: 2 bit channel
- -: 2 bit unknown
- T: 16 bit temperature, stored as int / 10 (e.g. 376 = 37.6C)
- C: 8 bit checksum

Checksum algorithm:
- Low nibble: sum of low nibbles of bytes 0-2, mod 16
- High nibble: ID-specific
  - ID=4: single bit = (hi_sum + overflow) & 1, upper 3 bits are 0
    where hi_sum = (b0>>4) + (b1>>4) + (b2>>4)
    and overflow = low_nibble_sum >> 4
  - ID=3,5: MSB (bit 3) = parity(b0_lo) XOR parity(b1_lo) XOR parity(b2)
            lower 3 bits = (2 + (hi_sum & 1)) XOR overflow

Sample data:

    3c 01 7f 3c : 38.3C
    3c 01 88 a5 : 39.2C
    3c 01 90 ad : 40.0C
    50 01 0c bd : 26.8C
    50 02 03 b5 : 51.5C
    4c 01 7f 0c : 38.3C (ID=4)

Flex decoder:

    rtl_433 -X 'n=thermor_a6n_132tx,m=OOK_PPM,s=1000,l=2000,g=2000,r=4000,repeats>=5'
*/

static int thermor_a6n_132tx_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row;

    if (bitbuffer->num_rows == 1 && bitbuffer->bits_per_row[0] == 32) {
        row = 0;
    } else {
        row = bitbuffer_find_repeated_row(bitbuffer, 5, 32);
        if (row < 0) {
            return DECODE_ABORT_EARLY;
        }
    }

    if (bitbuffer->bits_per_row[row] != 32) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[row];

    // Checksum low nibble: sum of low nibbles of payload bytes (mod 16)
    int lo_sum = (b[0] & 0x0f) + (b[1] & 0x0f) + (b[2] & 0x0f);
    int overflow = lo_sum >> 4;
    if ((lo_sum & 0x0f) != (b[3] & 0x0f)) {
        return DECODE_FAIL_MIC;
    }

    // Checksum high nibble: ID-specific
    int id = (b[0] >> 4) & 0x0f;
    int hi_sum = (b[0] >> 4) + (b[1] >> 4) + (b[2] >> 4);
    int chk_hi = b[3] >> 4;

    if (id == 4) {
        // ID=4: high nibble is 1 bit, upper 3 bits are 0
        if (chk_hi != ((hi_sum + overflow) & 1)) {
            return DECODE_FAIL_MIC;
        }
    } else {
        // ID=3,5: MSB = parity(b0_lo) XOR parity(b1_lo) XOR parity(b2)
        //         lower 3 bits = (2 + (hi_sum & 1)) XOR overflow
        int chk_hi_msb = parity8(b[0] & 0x0f) ^ parity8(b[1] & 0x0f) ^ parity8(b[2]);
        int chk_hi_low = (2 + (hi_sum & 1)) ^ overflow;
        if (chk_hi != ((chk_hi_msb << 3) | chk_hi_low)) {
            return DECODE_FAIL_MIC;
        }
    }

    int channel = (b[0] >> 2) & 0x03;
    int temp_raw = (b[1] << 8) | b[2];
    float temperature_c = temp_raw * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",          "",            DATA_STRING, "Thermor-A6N-132TX",
            "id",             "ID",          DATA_INT,    id,
            "channel",        "Channel",     DATA_INT,    channel,
            "temperature_C",  "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature_c,
            "mic",            "Integrity",   DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "temperature_C",
        "mic",
        NULL,
};

r_device const thermor_a6n_132tx = {
        .name        = "Thermor A6N 132TX temperature sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .gap_limit   = 2000,
        .reset_limit = 4000,
        .decode_fn   = &thermor_a6n_132tx_decode,
        .fields      = output_fields,
};
