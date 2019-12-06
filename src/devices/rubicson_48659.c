/** @file
    Rubicson 48659 meat thermometer.

    Copyright (C) 2019 Benjamin Larsson.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Rubicson 48659 meat thermometer.

{32} II 12 TT MN

- I = power on id
- 1 = 0UUU U follows temperature [0-7]
- 2 = XSTT S = sign, TT = temp high bits (2) X=unknown
- T = Temp in Farenhight
- M = xorsum high nibble
- M = xorsum low nibble (add ^4 to match output)


{32} 01 08 71 d4    45
{32} 01 18 73 e6    46
{32} 01 28 75 f8    47
{32} 01 38 77 0a    48
{32} 01 48 79 1c    49
{32} 01 50 7a 25    50C  122F
{32} 01 60 7c 37    51C  124F
{32} 01 70 7e 49    52
{32} 01 00 80 db    53
{32} 01 10 82 ed    54
{32} 01 18 83 f6    55
{32} 01 28 85 08    56
{32} 01 38 87 1a    57          {32} 0b 68 4d 1a    25
{32} 01 48 89 2c    58
{32} 01 58 8b 3e    59
{32} 01 60 8c 47    60
{32} 01 70 8e 59    61
{32} 01 00 90 eb    62
{32} 01 10 92 fd    63
{32} 01 20 94 0f    64
{32} 01 28 95 18    65
{32} 01 38 97 2a    66
{32} 01 48 99 3c    67
{32} 01 58 9b 4e    68
{32} 01 68 9d 60    69
{32} 01 70 9e 69    70
{32} 01 00 a0 fb    71
{32} 01 10 a2 0d    72
{32} 01 20 a4 1f    73
{32} 01 30 a6 31    74
{32} 01 38 a7 3a    75
{32} 01 48 a9 4c    76


battery

{32} cc 38 67 c5    39
{32} cc 08 61 8f    36
{32} cc 78 5f fd    34
{32} cc 70 5e f4    33
{32} cc 50 5a d0    32
{32} cc 30 56 ac    30
{32} cc 18 53 91    28
{32} cc 08 51 7f    27
{32} cc 78 4f ed    26


battery

{32} f0 18 43 a5    19
{32} f0 30 46 c0    21
{32} f0 40 48 d2    22
{32} f0 50 4a e4    23
{32} f0 60 4c f6    24
{32} f0 78 4f 11    26

battery change for each value

{32} d2 60 4c d8    24
{32} 01 60 4c 07    24
{32} 20 60 4c 26    24
{32} c3 60 4c c9    24
{32} ae 60 4c b4    24
{32} 98 60 4c 9e    24
{32} 27 60 4c 2d    24
{32} 5d 60 4c 63    24

{32} 49 68 4d 58    25
{32} d9 68 4d e8    25
{32} 36 68 4d 45    25
{32} 0b 68 4d 1a    25
{32} 63 68 4d 72    25
{32} 80 68 4d 8f
{32} 3c 68 4d 4b
{32} 97 68 4d a6
{32} 37 68 4d 46
{32} 64 68 4d 73
{32} 76 68 4d 85
{32} f6 68 4d 05
{32} fa 68 4d 09
{32} d6 68 4d e5
{32} d3 68 4d e2
{32} 01 68 4d 10
{32} 25 68 4d 34
{32} e0 68 4d ef
{32} 22 68 4d 31
{32} 56 68 4d 65
{32} 53 68 4d 62

{32} 0b 78 4f 2c    26

{32} 23 28 65 0a    38
{32} 23 70 6e 5b    43
{32} 23 00 70 ed    44

{32} 23 60 dc b9    104
{32} 23 78 df d4    106
{32} 23 28 e5 8a    109
{32} 23 08 f1 76    116
{32} 23 40 f8 b5    120
{32} 23 60 fc d9    122
{32} 23 19 03 99    128
{32} 23 61 0c ea    131
{32} 23 19 13 a9    135
{32} 23 39 17 cd    138
{32} 23 01 20 9e    142
{32} 23 69 2d 13    149
{32} 23 01 30 ae    151
{32} 23 21 34 d2    153
{32} 23 31 36 e4    154
{32} 23 39 37 ed    155
{32} 23 59 3b 11    157
{32} 23 69 3d 23    158
{32} 23 79 3f 35    159

{32} 23 79 3f 35    159
{32} 1a 70 0e f2    -10
{32} 1a 18 03 8f    -16
{32} 1a 8c 01 01    -18
{32} 1a 9c 03 13    -19
{32} 1a b4 06 2e    -22
{32} 1a d4 0a 52    -23
{32} 1a e4 0c 64    -24
*/

static int rubicson_48659_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row;
    bitrow_t *bb = bitbuffer->bb;
    unsigned int id;
    float temp_f;
    data_t *data;

    // Compare first four bytes of rows that have 32 or 33 bits.
    // more then 25 repeats are not uncommon
    row = bitbuffer_find_repeated_row(bitbuffer, 10, 32);
    if (row < 0)
        return 0;

    if ((bitbuffer->bits_per_row[row] > 33) || (bitbuffer->bits_per_row[row] < 10))
        return 0;

    id = bb[row][0];
    // 1 sign bit and 10 bits for the value
    temp_f = ((bb[row][1] & 0x04) >> 2) ? -1 : 1 * (((bb[row][1] & 0x3) << 8) + bb[row][2]);

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, _X("Rubicson-48659","Rubicson 48659"),
            "id",            "Id",          DATA_INT,    id,
            "temperature_F", "Temperature", DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp_f,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "temperature_F",
        NULL,
};

r_device rubicson_48659 = {
        .name        = "Rubicson 48659 Thermometer",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 940,
        .long_width  = 1900,
        .gap_limit   = 2000,
        .reset_limit = 4000,
        .decode_fn   = &rubicson_48659_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
