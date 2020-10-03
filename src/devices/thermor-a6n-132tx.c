
/** @file
 *  Thermor A6N 132TX temperature sensor
 *
 * Copyright (C) 2020 Jon Klixbüll Langeland
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
/**
32 byte frame

  {32} e4 01 8f 34 : 11100100 00000001 10001111 00110100

  I: ID
  C: Channel
  -: unknown
  B: Battery state
  T: 12 bit Temp stored as int / 10  376 = 37.6C
  x: 8 bit checksum


    IIIICC-- TTTTTTTT TTTTTTTT --------
    00111100 00000001 10000001 10111110 =   38C, 101F
    11100100 00000001 10001111 00110100 =   39C, 103F
    11100100 00000001 10001101 10110010 =   39C, 103F

    11100100 00000001 10001100 00110001 =   39C, 103F
    00111100 00000010 11101111 00101101 =   75C, 167F

  

  flex decoder with -X 'n=sensor,m=OOK_PPM,s=1000,l=2000,g=2000,r=4000,repeats>=3'

*/

#include "decoder.h"

static int thermor_a6n_132tx_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // 32 bit, repeated multiple times (technically it is repeated 11 times, look for 5 identical versions)
    int row = bitbuffer_find_repeated_row(bitbuffer, 5, 32);

    if (row < 0) {
        return DECODE_ABORT_EARLY;
    };

    // we're looking for exactly 32 bits
    if (bitbuffer->bits_per_row[row] != 32) {
        return DECODE_ABORT_LENGTH;
    };


    // Data buffer
    uint8_t *bytes = bitbuffer->bb[row];
    char buffer_string[80] = "";
    for (unsigned bit = 0; bit < 32; ++bit) {
        if (bytes[bit / 8] & (0x80 >> (bit % 8))) {
            sprintf(buffer_string + strlen(buffer_string), "1");
        }
        else {
            sprintf(buffer_string + strlen(buffer_string), "0");
        }
        if ((bit % 8) == 7) // Add byte separators
            sprintf(buffer_string + strlen(buffer_string), " ");
    }

    // Identifier
    uint8_t identifier_buffer[4];
    bitbuffer_extract_bytes(bitbuffer, row, 0, identifier_buffer, 4);
    int identifier = ((identifier_buffer[0] >> 4) | identifier_buffer[1]);


    // Channel
    uint8_t channel_buffer[2];
    bitbuffer_extract_bytes(bitbuffer, row, 4, channel_buffer, 2);
    int channel = ((channel_buffer[0] >> 6) | channel_buffer[1]);

    // Temperature
    uint8_t temperature_buffer[16];
    bitbuffer_extract_bytes(bitbuffer, row, 8, temperature_buffer, 16);
    int temperature_raw = ((temperature_buffer[0] << 8) | temperature_buffer[1]);

    // Output data
    data_t *data;
    /* clang-format off */
    data = data_make(
            "model",                "",             DATA_STRING,    "Thermor A6N 132TX",
            "identifier",           "identifier",   DATA_INT,       identifier,
            "channel",              "channel",      DATA_INT,       channel,
            "temperature_C",        "temperature",  DATA_FORMAT,    "%.01f C", DATA_DOUBLE, temperature_raw * 0.1,
            "buffer_string",        "buffer",       DATA_STRING,    buffer_string,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "identifier",
        "channel",
        "temperature_C",
        "buffer_string",
        NULL,
};

r_device thermor_a6n_132tx = {
        .name        = "Thermor A6N 132TX temperature sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .gap_limit   = 2000,
        .reset_limit = 4000,
        .decode_fn   = &thermor_a6n_132tx_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
