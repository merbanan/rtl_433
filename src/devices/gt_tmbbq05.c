/** @file
    Globaltronics Quigg BBQ GT-TMBBQ-05

    Copyright (C) 2019 Olaf Glage

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
*/
/**
Globaltronics Quigg BBQ GT-TMBBQ-05.

BBQ thermometer sold at Aldi (germany)
Simple device, no possibility to select channel. Single temperature measurement.

The temperature is transmitted in Fahrenheit with an addon of 90. Accuracy is 10 bit. No decimals.
One data row contains 33 bits and is repeated 8 times. Each followed by a 0-row. So we have 16 rows in total.
First 9 bits seem to be a static header, I assume for sync purposes (001001001).
Next 8 bits contain the lower 8 bits of the temperature.
Next 8 bits are static. Purpose unknown. Maybe hard wired channel inside device?
Next 2 bits contain the upper 2 bits of the temperature
Next 6 bits vary, but purpose is unknown. I assume a kind of CRC as they are the same for same temperatures.

Here's the data I used to reverse engineer, more sampes in rtl_test
001001001100010000111100110010110  HI
001001001010101010111100110010000  507
001001001010011010111100110010111  499
001001001110101110111100101010110  381
001001001110000000111100101011110  358
001001001001011010111100101010001  211
001001001001000000111100101000011  198
001001001111010110111100100000110  145
001001001101100010111100100001001  89
001001001101011010111100100010101  83
001001001101011010111100100010101  83
001001001101010110111100100010011  81
001001001101010010111100100000000  79
001001001101010000111100100010000  78
001001001101001110111100100011111  77
001001001101001100111100100001101  76
001001001101001010111100100001100  75
001001001101001010111100100001100  75
001001001101000110111100100001010  73
001001001100010100111100100010000  48
001001001011011110111100100000010  21
001001001011001110111100100011011  13
001001001010010010111100100011011  LO

Frame structure:

    Byte:             1        2        3
    Type:   001001001 tttttttt ssssssss TTcccccc

- tt = temperature+90 F lower 8 bits
- TT = temperature+90 F upper 2 bits
- ss = unknown, static -> maybe channel?
- cc = unknown, changes -> maybe CRC?
*/

#include "decoder.h"

static int gt_tmbbq05_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitrow_t *bb = bitbuffer->bb;
    data_t *data;

    if (decoder->verbose > 1) {
        bitbuffer_printf(bitbuffer, "%s: Possible Quigg BBQ: ", __func__);
    }

    // 33 bit, repeated multiple times (technically it is repeated 8 times, look for 5 identical versions)
    int r = bitbuffer_find_repeated_row(bitbuffer, 5, 33);

    // we're looking for exactly 33 bits
    if (r < 0 || bitbuffer->bits_per_row[r] != 33)
        return DECODE_ABORT_LENGTH;

    // remove the 9 leading bits and extract the 3 bytes carrying the data
    uint8_t data_bytes[3];
    bitbuffer_extract_bytes(bitbuffer, r, 9, data_bytes, 24);

    // concat the upper bits to the lower bits and substract the fixed offset 90
    int tempf = ((data_bytes[2] >> 6) | data_bytes[0]) - 90;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "GT-TMBBQ05",
            "temperature_F",    "Temperature",  DATA_FORMAT, "%.02f F", DATA_DOUBLE, (double)tempf,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "temperature_F",
        NULL,
};

r_device gt_tmbbq05 = {
        .name        = "Globaltronics QUIGG GT-TMBBQ-05",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .gap_limit   = 4100,
        .reset_limit = 9000,
        .decode_fn   = &gt_tmbbq05_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
