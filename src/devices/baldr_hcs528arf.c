/** @file
    Baldr HCS528ARF Pool Thermometer sensor.

    Copyright (C) 2025 Bruno OCTAU, Christian W. Zuckschwerdt, \@endmarsfr

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Baldr HCS528ARF Pool Thermometer sensor.

Brand:

- Fujian Baldr Technology Co., Ltd.

Reference:

- Baldr HCS528ARF Pool Thermometer sensor works with Baldr HCS015T2H Display Station

S.a. Issue 3333

OOK PCM, MC, Invert and Reflect, the message is repeated 10 times
The protocol is very similar to Rainpoint HCS012ARF, here 11 bytes instead of 10.

Flex:

    rtl_433 -X 'n=name,m=OOK_PCM,s=300,l=300,g=700,r=1100'

Samples:

    data      : 9966965559666955995595569555a6a66555aaaa69aa8
    MC decoded  a5 90 25 60 a0 81 80 dd 40 ff 6f 1
    MC Zerobit  25 90 25 60 a0 81 80 95 40 ff 27 8

Data Layout (before reflect):

    Byte Position  0  1  2  3  4  5  6  7  8  9 10
    Sample        a5 90 25 60 a0 81 80 dd 40 ff 6f 1
    Data          SS II II II II FB FF TT TF FF CC T

- SS:{8}  0xa5 , header sync word (OOK PCM then MC decoded) or 0x25 if directly OOK MC Zerobit decoded
- II:{48} Sensor ID ?
- FF:{x}  Fixed value
- B1:{1}  Low Battery flag = 1, Good Battery = 0  (To be confirmed, assumption from rainpoint hcs012arf)
- B2:{1}  Powered on, batteries are inserted = 1, then always = 0  (To be confirmed, assumption from rainpoint hcs012arf)
- TT:{12} Temperature, Fahrenheit, scale 10
- CC:{8}  Checksum, addition of previous reflected bytes except sync word.

*/

static int baldr_hcs528arf_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_t decoded = {0};

    // Find repeats
    int row = bitbuffer_find_repeated_row(bitbuffer, 4, 179);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[row] > 179) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_manchester_decode(bitbuffer, row, 0, &decoded, 11 * 2 * 8);
    bitbuffer_invert(&decoded);

    uint8_t *b = decoded.bb[0];
    reflect_bytes(b, 11);

    decoder_log_bitrow(decoder, 2, __func__, b, 88, "MSG");

    if (b[0] != 0xa5) { // Header sync word
        decoder_logf(decoder, 2, __func__, "Wrong Sync Word %02x expected 0xa5", b[0]);
        return DECODE_ABORT_EARLY;
    }

    // Checksum
    int sum = add_bytes(&b[1], 9); // header not part of the sum
    if ((sum & 0xff) != b[10]) {
        decoder_logf(decoder, 2, __func__, "Checksum failed %04x vs %04x", b[10], sum);
        return DECODE_FAIL_MIC;
    }

    int id       = b[4] << 24 | b[3] << 16 | b[2] << 8 | b[1]; // little endian
    int bat_low  = (b[5] & 0x02) >> 1;
    int temp_raw = (b[8] & 0x0F) << 8 | b[7]; // little endian
    float temp_f = temp_raw * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING,    "Baldr-HCS528ARF",
            "id",               "",             DATA_FORMAT,    "%08x",   DATA_INT,       id,
            "battery_ok",       "Battery",      DATA_INT,       !bat_low,
            "temperature_F",    "Temperature",  DATA_FORMAT,    "%.1f F", DATA_DOUBLE, temp_f,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_F",
        NULL,
};

r_device const baldr_hcs528arf = {
        .name        = "Baldr HCS528ARF Pool Thermometer sensor",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 320,
        .long_width  = 320,
        .gap_limit   = 700,
        .reset_limit = 1000,
        .decode_fn   = &baldr_hcs528arf_decode,
        .fields      = output_fields,
};
