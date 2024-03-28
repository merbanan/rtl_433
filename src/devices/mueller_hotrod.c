/** @file
    Mueller Hot Rod water meter.

    Copyright (C) 2024 Christian W. Zuckschwerdt <zany@triq.net>
    Copyright (C) 2024 Bruno OCTAU (ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Mueller Hot Rod water meter.

S.a. #2719 Decoder desired for Mueller Systems Hot Rod transmitter (water meter), open by "howardtopher", related to Hod Rod v2 transmitter
S.a. #2752 Decoder for Mueller Hot Rod V1 Water Meter Transmitter, open by "dolai1", related to Hod Rod v1 transmitter

Both version v1 and v2 protocols look same format.

Flex decoder:

    rtl_433 -X 'n=hotrod,m=FSK_PCM,s=26,l=26,r=2500,preamble=feb100'

Raw RF Signal:

    {136}ffffffffffd62002884cc2c092f1201f80
    {135}fff555555fd62002884cc2c092f1201f80
    {135}ffeaaaaabfac40051099858125e2403f00
    {134}000002aabfac40051099858125e54015c0
    {134}00000000000040051099858125e54015c0

The preamble is not stable because of the GFSK encoding not well handle by rtl_433.

Data layout:
    YY YY YY  0  1  2  3  4  5  6  7  8  9 10 11 ...
    fe b1 00 II II II II GG GG GF FF CC ?? ?? ?? ...

- YY: {24} Sync word 0xfeb100
- II: {32} Device ID
- GG: {20} 5 niblles BCD water cumulative volume, gallons, scale 100.
- FF: {12} Flag, protocol version, battery_low ??? to be confirmed later.
- CC: {8} CRC-8/UTI, poly 0x07, init 0x00, xorout 0x55
- ??: extra bit not used, related to GFSK/FSK encoding.


*/

static int mueller_hotrod_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xfe, 0xb1, 0x00};

    if (bitbuffer->num_rows != 1) {
        decoder_log(decoder, 1, __func__, "Row check failed");
        return DECODE_ABORT_EARLY;
    }

    // 3 byte for the sync word + 9 byte for data = 96 bits in total, too short if less
    if (bitbuffer->bits_per_row[0] < 96) {
        decoder_log(decoder, 1, __func__, "Len before preamble check failed");
        return DECODE_ABORT_LENGTH;
    }

    // Find the preamble
    unsigned pos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);
    if ((pos + 9 * 8) >= bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 1, __func__, "Len after preamble check failed");
        return DECODE_ABORT_EARLY;
    }

    uint8_t b[9];
    bitbuffer_extract_bytes(bitbuffer, 0, pos + 24, b, 72); // 9 x 8 bit
    decoder_log_bitrow(decoder, 1, __func__, b, sizeof(b) * 8, "MSG");

    // poly 0x07, init 0x00, xorout 0x55
    int crc_calc = crc8(b, 8, 0x07, 0x00) ^ 0x55;
    if (crc_calc != b[8]) {
        decoder_logf(decoder, 1, __func__, "CRC check failed : %0x %0x", b[8], crc_calc);
        return 0;
    }

    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x%02x", b[0], b[1], b[2], b[3]);

    // 5 nibbles BCD (20 bit) x 100 = volume_gal
    int volume = (((b[4] & 0xf0) >> 4) * 10000 + (b[4] & 0x0f) * 1000 + ((b[5] & 0xf0) >> 4) * 100 + (b[5] & 0x0f) * 10 + ((b[6] & 0xf0) >> 4)) * 100;
    int flag1  = b[6] & 0x0f;
    int flag2  = b[7] & 0xff;

    // volume could be from 6 or 7 nibbles, if 6 nibbles, scale 10, if 7 nibbles, scale 1. No more flag1 used to calculate the volume. To be confirmed.
    // 6 nibbles BCD (24 bit) x 10 = volume_gal
    //int volume = (((b[4] & 0xf0) >> 4)*100000+(b[4] & 0x0f)*10000+((b[5] & 0xf0) >> 4)*1000+(b[5] & 0x0f)*100+((b[6] & 0xf0) >> 4)*10+(b[6] & 0x0f)) * 10;
    //int flag2  = b[7] & 0xff;

    // 7 nibbles BCD (28 bit) = volume_gal
    //int volume = ((b[4] & 0xf0) >> 4)*1000000+(b[4] & 0x0f)*100000+((b[5] & 0xf0) >> 4)*10000+(b[5] & 0x0f)*1000+((b[6] & 0xf0) >> 4)*100+(b[6] & 0x0f)*10+((b[7] & 0xf0) >> 4);
    //int flag2  = b[7] & 0x0f;

    /* clang-format off */
    data_t *data = data_make(
            "model",       "",          DATA_STRING, "Mueller-HotRod",
            "id",          "",          DATA_STRING, id_str,
            "volume_gal",  "Volume",    DATA_FORMAT, "%u gal", DATA_INT, volume,
            "flag1",       "Flag 1",    DATA_FORMAT, "%x" , DATA_INT, flag1,
            "flag2",       "Flag 2",    DATA_FORMAT, "%x" , DATA_INT, flag2,
            "mic",         "Integrity", DATA_STRING, "CRC",
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "volume_gal",
        "flag1", "flag2",
        "mic",
        NULL,
};

r_device const mueller_hotrod = {
        .name        = "Mueller Hot Rod water meter",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 26,
        .long_width  = 26,
        .reset_limit = 2500,
        .decode_fn   = &mueller_hotrod_decode,
        .fields      = output_fields,
};
