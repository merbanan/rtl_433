/** @file
    Generic Holtek HT12E decoder.

    Copyright (C) 2021 Marcos Del Sol Vives

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

static int ht12e_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b;
    uint32_t bits;
    int address;
    data_t *data;

    if (bitbuffer->bits_per_row[0] != 13)
        return DECODE_ABORT_LENGTH;

    b = (uint8_t *)bitbuffer->bb[0];

    // Unpack the data
    bits = reverse8(b[1]) << 8 | reverse8(b[0]);

    // First bit must be one
    if ((bits & 1) != 1)
        return 0;

    // Address are the next 12 bits
    address = bits >> 1;

    data = data_make(
            "model", "", DATA_STRING, _X("Holtek-HT12E", "Holtek HT12E remote"),
            "address", "Address", DATA_FORMAT, "0x%03X", DATA_INT, address,
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
        "model",
        "address",
        NULL,
};

// Average and max frequency in hertz for a HT12E with Rosc=1Mohm
#define FOSC_AVG 3000.0
#define FOSC_MAX 3500.0

// Duration of one time element in microseconds
#define TE_AVG (1e6 / FOSC_AVG)
#define TE_MAX (1e6 / FOSC_MAX)

r_device ht12e = {
        .name        = "Holtek HT12E remote",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 1 * TE_AVG,
        .long_width  = 2 * TE_AVG,
        .reset_limit = 8 * TE_AVG,
        .tolerance   = 2 * (TE_MAX - TE_AVG),
        .decode_fn   = &ht12e_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
