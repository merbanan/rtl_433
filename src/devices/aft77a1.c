/** @file
 *  Auriol AFT 77 A1 temperature sensor
 *
 * Copyright (C) 2020 Peter Shipley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
/**
42 byte frame

    {42} ba 01 78 02 2a 40 : 10111010 00000001 01111000 00000010 00101010 01

    10111010 00000001 01111000 00000010 00101010 01 =   37.6C
    IIIIIIII XXXXTTTT TTTTTTTT XXXXXXXX XXCCCCCC CC

  I: Device ID
  X: unknown
  T: 12 bit Temp stored as int / 10  376 = 37.6C
  C: 8 bit checksum

*/

#include "decoder.h"

static int aft77a1_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t buffy[6];
    data_t *data;
    int i;
    uint16_t temp_raw; // raw temperature from packet
    float temp_C;

    int16_t row;


    uint8_t device_id;      // derived checksum
    uint8_t chk_sum;      // derived checksum
    uint8_t checksum_dat; // checksum from packet

    row = bitbuffer_find_repeated_row(bitbuffer, 2, 42);

    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    if (decoder->verbose) {
        bitbuffer_extract_bytes(bitbuffer, row, 0, buffy, 42);
        bitrow_printf(buffy, 42, "%s packet", __func__);
    }

    bitbuffer_extract_bytes(bitbuffer, row, 34, buffy, 8);
    checksum_dat = buffy[0];

    bitbuffer_extract_bytes(bitbuffer, row, 8, buffy, 16);
    temp_raw = buffy[0] << 8 | buffy[1];

    if (decoder->verbose) {
        bitrow_printf(&checksum_dat, 8, "%s checksum_dat = %u %04X", __func__, checksum_dat, checksum_dat);
        bitrow_printf(buffy, 16, "%s temp_raw     = %u %04X buf = %u %04X", __func__, temp_raw, temp_raw, buffy[0], buffy[1]);
    }

    i = temp_raw >> 4;

    if (i < 10)
        chk_sum = 135;
    else if (i < 16)
        chk_sum = 151;
    else if (i < 20)
        chk_sum = 136;
    else
        chk_sum = 138;

    chk_sum += i;
    chk_sum += temp_raw & 0x0000000f;

    if (decoder->verbose)
        bitrow_printf(&chk_sum, 8, "%s chk_sum", __func__);

    if (chk_sum != checksum_dat) {
        if (decoder->verbose)
            (void)fprintf(stderr, "%s: checksum %hu !=  %hu\n", __func__, chk_sum, checksum_dat);
        return (DECODE_FAIL_MIC);
    }

    bitbuffer_extract_bytes(bitbuffer, row, 0, buffy, 8);
    device_id = buffy[0];

    temp_C = temp_raw / 10.0;

    /* clang-format off */
    data = data_make(
            "model",            "",               DATA_STRING,   _X("AFT77A1","Auriol AFT 77 A1 temperature sensor"),
            "id",               "Id",             DATA_INT,     device_id,
            // "battery",          "Battery?",     DATA_INT,     battery,
            "temperature_C",    "Temperature",    DATA_FORMAT,   "%.01f C",  DATA_DOUBLE,    temp_C,
             "mic",             "",               DATA_STRING,   "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        // "battery",
        "temperature_C",
        "mic",
        NULL,
};

//  flex -X 'n=aft77a1,m=OOK_PPM,s=2076,l=4124,g=4196,r=9196'

r_device aft77a1 = {
        .name        = "Auriol AFT 77 A1 temperature sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2076,
        .long_width  = 4124,
        .gap_limit   = 4800,
        .reset_limit = 10000,
        .decode_fn   = &aft77a1_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
