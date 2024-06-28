/** @file
    Arad/Master Meter Dialog3G water utility meter.

    Copyright (C) 2022 avicarmeli

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Arad/Master Meter Dialog3G water utility meter.

FCC-Id: TKCET-733

Message is being sent once every 30 seconds.
The message looks like:

    00000000FFFFFFFFFFFFFFSSSSSSSSXXCCCCCCXXXF?????????XFF

where:

- 00000000 is preamble.
- FFFFFFFFFFFFFF  is fixed in time and the same for other meters in the neighborhood. Probably gearing ratio. The payload is 3e690aec7ac84b.
- SSSSSSSS  is Meter serial number.  for instance fa1c9073 =>  fa1c90 = 09444602, little endian 73= 'S'
- XX no idea.
- CCCCCC is the counter reading little endian for instance a80600= 1704
- XXX no idea.
- F  is fixed in time and the same for other meters in the neighborhood. With payload of 5.
- ????????? probably some kind of CRC or checksum - here is where I need help.
- X is getting either 8 or 0 same for other meters in the neighborhood.
- FF is fixed in time and the same for other meters in the neighborhood.With payload f8.

Format string:

    56x SERIAL: <24dc 8x COUNTER: <24d hhhhhhhhhhhhhh  SUFFIX:hh

*/

static int arad_mm_dialog3g_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0x96, 0xf5, 0x13, 0x85, 0x37, 0xb4}; // 48 bit preamble

    int row = bitbuffer_find_repeated_row(bitbuffer, 1, 168); // expected 1 row with minimum of 48+120= 168 bits.
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, 48);
    start_pos += 48; // skip preamble

    if ((bitbuffer->bits_per_row[row] - start_pos) < 120) {
        return DECODE_ABORT_LENGTH; // short buffer or preamble not found
    }

    bitbuffer_invert(bitbuffer);

    uint8_t b[15];
    bitbuffer_extract_bytes(bitbuffer, row, start_pos, b, 120);

    int serno    = b[0] | (b[1] << 8) | (b[2] << 16); // 24 bit little endian Meter Serial number
    int wreadraw = b[5] | (b[6] << 8) | (b[7] << 16); // 24 bit little endian Meter water consumption reading
    float wread = wreadraw * 0.1f;

    char sernoout[10];
    sprintf(sernoout, "%08u%c", serno, b[3] - 32);

    /* clang-format off */
    data_t *data = data_make(
            "model",       "",               DATA_STRING,    "AradMsMeter-Dialog3G",
            "id",          "Serial No",      DATA_STRING,    sernoout,
            "volume_m3",    "Volume",        DATA_FORMAT,    "%.1f m3",  DATA_DOUBLE, wread,
            //"mic",         "Integrity",      DATA_STRING,    "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "waterread",
        //"mic",
        NULL,
};

r_device const arad_ms_meter = {
        .name        = "Arad/Master Meter Dialog3G water utility meter",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 8.4,
        .long_width  = 8.4, // not used
        .reset_limit = 30,
        .decode_fn   = &arad_mm_dialog3g_decode,
        .disabled    = 1, // checksum not implemented
        .fields      = output_fields,
};
