
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

  I: Device ID
  -: unknown
  B: Battery state
  T: 12 bit Temp stored as int / 10  376 = 37.6C
  C: 8 bit checksum

    IIIIIIII -------B -------- --------
    IIIIIIII -------B TTTTTTTT TTTT----
    00111100 00000001 10000001 10111110 =   38C, 101F
    11100100 00000001 10001111 00110100 =   39C, 103F
    11100100 00000001 10001101 10110010 =   39C, 103F
    11100100 00000001 10001100 00110001 =   39C, 103F

  

  flex decoder with -X 'n=sensor,m=OOK_PPM,s=1000,l=2000,g=2000,r=4000,repeats>=3'

*/

#include "decoder.h"

static int thermor_a6n_132tx_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    //fprintf(stderr,"Thermor A6N 132TX temperature sensor: ");

    // 32 bit, repeated multiple times (technically it is repeated 11 times, look for 5 identical versions)
    int r = bitbuffer_find_repeated_row(bitbuffer, 5, 32);

    // we're looking for exactly 32 bits
    if (r < 0 || bitbuffer->bits_per_row[r] != 32) {
        return DECODE_ABORT_LENGTH;
    };

    

    char data_string[80];
    int channel;
    int battery;
    int temp_raw;


    for (unsigned bit = 0; bit < 32; ++bit) {
        if (bitbuffer->bb[r][bit / 8] & (0x80 >> (bit % 8))) {
            sprintf(data_string + strlen(data_string), "1");
        }
        else {
            sprintf(data_string + strlen(data_string), "0");
        }
        if ((bit % 8) == 7) // Add byte separators
            sprintf(data_string + strlen(data_string), " ");
    }

    channel  = bitrow_get_byte(bitbuffer->bb[r], 0);
    battery  = bitrow_get_byte(bitbuffer->bb[r], 8);
    temp_raw = 42;

    data_t *data;
    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, "Thermor A6N 132TX",
            "channel",       "Channel",     DATA_FORMAT, "%02x ", DATA_INT, channel,
            "battery_ok",    "Battery",     DATA_INT, battery,
            "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_raw * 0.1,
            "data_string",   "Data string", DATA_STRING, data_string,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "channel",
        "battery_ok",
        "temperature_C",
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
