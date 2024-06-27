/** @file

    Arad/Master Meter Dialog3G water utility meter.
    
    Copyright (C) 2022 avicarmeli
    Additional credits to participants of THE-THREAD
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**

Arad/Master Meter Dialog3G water utility meter.

FCC-Id: TKCET-733
Massage is being sent once every 30 second.
The massage look like that:
00000000FFFFFFFFFFFFFFSSSSSSSSXXCCCCCCXXXF?????????XFF
where:
00000000 is preamble.
FFFFFFFFFFFFFF  is fixed in time and the same for other meters in the neighborhood. Probably gearing ratio. The payload is 3e690aec7ac84b.
SSSSSSSS  is Meter serial number.  for instance fa1c9073 =>  fa1c90 = 09444602, little endian 73= 'S'
XX no idea.
CCCCCC is the counter reading little endian for instance a80600= 1704
XXX no idea.
F  is fixed in time and the same for other meters in the neighborhood. With payload of 5.
????????? probably some kind of CRC or checksum - here is where I need help.
X is getting either 8 or 0 same for other meters in the neighborhood.
FF is fixed in time and the same for other meters in the neighborhood.With payload f8.
*/

#include "decoder.h"

static int arad_mm_dialog3g_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0x96,0xf5,0x13,0x85,0x37,0xb4}; // 48 bit preamble
    int row;
    data_t *data;
    bitbuffer_t databits = {0};


   // fprintf(stderr,"arad_mm_dialog3g callback was triggered :-) \n");
   // bitbuffer_print(bitbuffer);

    row = bitbuffer_find_repeated_row(bitbuffer, 1, 168); // expected 1 row with minimum of 48+120= 168 bits.
    if (row < 0)
        return DECODE_ABORT_EARLY;

    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, 48);
    start_pos += 48; // skip preamble

    if ((bitbuffer->bits_per_row[row] - start_pos) < 120)
        return DECODE_ABORT_LENGTH; // short buffer or preamble not found

    bitbuffer_invert(bitbuffer);

    //bitbuffer_print(bitbuffer);

    bitbuffer_extract_bytes(bitbuffer, row,start_pos, mdata, 120);

    uint32_t serno = mdata[0]| (mdata[1] << 8) | (mdata[2] << 16) | (0 << 24); //24 bit little endian Meter Serial number
    uint32_t wreadraw = mdata[5]| (mdata[6] << 8) | (mdata[7] << 16) | (0 << 24); //24 bit little endian Meter water consumption reading

    char sernoout[10];

    sprintf(sernoout, "%08u%c", serno, mdata[3]-32);

    float wread = wreadraw;

    wread=wread/10;

     /* clang-format off */
    data = data_make(
        "model",       "",               DATA_STRING,    "Arad-MsMeter",
        "id",          "Serial No",      DATA_STRING,    sernoout,
        "waterread",   "Water Reading",  DATA_FORMAT,    "%.1f M^3", DATA_DOUBLE, wread,
        "mic",         "Integrity",      DATA_STRING,    "CHECKSUM",
        NULL);
     /* clang-format on */
 
    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "waterread",
        "mic",
        NULL,
};

r_device arad_ms_meter = {
        .name        = "Arad/Master Meter Dialog3G water utility meter",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 8.4,
        .long_width  = 0, //not used
        .reset_limit = 30,
        .decode_fn   = &arad_mm_dialog3g_decode,
        .disabled       = 1, // stop debug output from spamming unsuspecting users
        .fields      = output_fields,
};
