/** @file
    Amazon Basics Meat Thermometer

    Copyright (C) 2021 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Amazon Basics Meat Thermometer

Manchester encoded PCM signal.

[00] {48} e4 00 a3 01 40 ff

II 00 UU TT T0 FF

I - power on random id
0 - zeros
U - Unknown
T - bcd coded temperature
F - ones


*/
#include "decoder.h"

#define SYNC_PATTERN_START_OFF 72

// Convert two BCD encoded nibbles to an integer
static unsigned bcd2int(uint8_t bcd)
{
    return 10 * (bcd >> 4) + (bcd & 0xF);
}

static int abmt_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row;
    float temp_c;
    bitbuffer_t packet_bits = {0};
    unsigned int id;
    data_t *data;
    unsigned bitpos = 0;
    uint8_t *b;
    int16_t temp;
    uint8_t const sync_pattern[3] = {0x55, 0xAA, 0xAA};

    // Find repeats
    row = bitbuffer_find_repeated_row(bitbuffer, 4, 90);
    if (row < 0)
        return DECODE_ABORT_EARLY;

    if (bitbuffer->bits_per_row[row] > 120)
        return DECODE_ABORT_LENGTH;

    // search for 24 bit sync pattern
    bitpos = bitbuffer_search(bitbuffer, row, bitpos, sync_pattern, 24);
    // if sync is not found or sync is found with to little bits available, abort
    if ((bitpos == bitbuffer->bits_per_row[row]) || (bitpos < SYNC_PATTERN_START_OFF))
        return DECODE_FAIL_SANITY;

    // sync bitstream
    bitbuffer_manchester_decode(bitbuffer, row, bitpos - SYNC_PATTERN_START_OFF, &packet_bits, 48);
    bitbuffer_invert(&packet_bits);

    b      = packet_bits.bb[0];
    id     = b[0];
    temp   = bcd2int(b[3]) * 10 + bcd2int(b[4] >> 4);
    temp_c = (float)temp;

    /* clang-format off */
     data = data_make(
             "model",         "",            DATA_STRING, "Basics-Meat",
             "id",            "Id",          DATA_INT,    id,
             "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
             NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_C",
        NULL,
};

r_device const abmt = {
        .name        = "Amazon Basics Meat Thermometer",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 550,
        .long_width  = 550,
        .gap_limit   = 2000,
        .reset_limit = 5000,
        .decode_fn   = &abmt_callback,
        .fields      = output_fields,
};
