/** @file
    Inovalley kw9015b rain and Temperature weather station.

    Copyright (C) 2015 Alexandre Coffignal

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Inovalley kw9015b rain and Temperature weather station.

Data layout:

    IIIIIIII RRRRtttt TTTTTTTT rrrrrrrr CCCC

- I : 8-bit ID
- T : 12-bit Temp in C, signed, scaled by 10
- R : 12-bit Rain
- C : 4-bit Checksum (nibble sum)
*/

#include "decoder.h"

static int kw9015b_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int row;
    uint8_t *b;
    int temp_raw, rain, device;
    unsigned char chksum;
    float temp_c;

    row = bitbuffer_find_repeated_row(bitbuffer, 3, 36);
    if (row < 0)
        return DECODE_ABORT_EARLY;

    if (bitbuffer->bits_per_row[row] > 36)
        return DECODE_ABORT_LENGTH;

    b = bitbuffer->bb[row];

    device   = reverse8(b[0]);
    temp_raw = (int16_t)((reverse8(b[2]) << 8) | (reverse8(b[1]) & 0xf0)); // sign-extend
    temp_c   = (temp_raw >> 4) * 0.1f;
    rain     = ((reverse8(b[1]) & 0x0f) << 8) | reverse8(b[3]);
    chksum   = ((reverse8(b[0]) >> 4) + (reverse8(b[0]) & 0x0f) +
              (reverse8(b[1]) >> 4) + (reverse8(b[1]) & 0x0f) +
              (reverse8(b[2]) >> 4) + (reverse8(b[2]) & 0x0f) +
              (reverse8(b[3]) >> 4) + (reverse8(b[3]) & 0x0f));

    if ((chksum & 0x0f) != (reverse8(b[4]) & 0x0f))
        return DECODE_FAIL_MIC;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, _X("Inovalley-kw9015b","Inovalley kw9015b"),
            "id",               "",             DATA_INT,    device,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "rain",             "Rain Count",   DATA_INT,    rain,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *kw9015b_csv_output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "rain",
        NULL,
};

r_device kw9015b = {
        .name        = "Inovalley kw9015b, TFA Dostmann 30.3161 (Rain and temperature sensor)",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .gap_limit   = 4800,
        .reset_limit = 10000,
        .decode_fn   = &kw9015b_callback,
        .disabled    = 1,
        .fields      = kw9015b_csv_output_fields,
};
