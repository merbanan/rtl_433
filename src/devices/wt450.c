/** @file
    WT450 wireless weather sensors protocol.

    Tested devices:
    WT260H
    WT405H

    Copyright (C) 2015 Tommy Vestermark
    Copyright (C) 2015 Ladislav Foldyna

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/**
source from:
http://ala-paavola.fi/jaakko/doku.php?id=wt450h

- The signal is FM encoded with clock cycle around 2000 µs
- No level shift within the clock cycle translates to a logic 0
- One level shift within the clock cycle translates to a logic 1
- Each clock cycle begins with a level shift
- My timing constants defined below are those observed by my program

    +---+   +---+   +-------+       +  high
    |   |   |   |   |       |       |
    |   |   |   |   |       |       |
    +   +---+   +---+       +-------+  low
    ^       ^       ^       ^       ^  clock cycle
    |   1   |   1   |   0   |   0   |  translates as

Each transmission is 36 bits long (i.e. 72 ms).

Data is transmitted in pure binary values, NOT BCD-coded.

Outdoor sensor transmits data temperature, humidity.
Transmissions also include channel code and house code. The sensor transmits
every 60 seconds 3 packets.

    1100 0001 | 0011 0011 | 1000 0011 | 1011 0011 | 0001
    xxxx ssss | ccxx bhhh | hhhh tttt | tttt tttt | sseo

- x: constant
- s: House code
- c: Channel
- b: battery low indicator (0=>OK, 1=>LOW)
- h: Humidity
- t: Temperature, 12 bit, offset 50, scale 16
- s: sequence number of message repeat
- e: parity of all even bits
- o: parity of all odd bits
*/

#include "decoder.h"

static int wt450_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b = bitbuffer->bb[0];
    uint8_t humidity;
    uint8_t temp_whole;
    uint8_t temp_fraction;
    uint8_t house_code;
    uint8_t channel;
    uint8_t battery_low;
    int seq;
    float temp;
    uint8_t parity;
    data_t *data;

    if (bitbuffer->bits_per_row[0] != 36) {
        if (decoder->verbose)
            fprintf(stderr, "wt450_callback: wrong size of bit per row %d\n",
                    bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH;
    }

    if (b[0]>>4 != 0xC) {
        if (decoder->verbose)
            bitbuffer_printf(bitbuffer, "wt450_callback: wrong preamble\n");
        return DECODE_ABORT_EARLY;
    }

    parity = xor_bytes(b, 5);
    parity ^= (parity >> 4);
    parity ^= (parity >> 2);
    parity &= 0x3;

    if (parity) {
        if (decoder->verbose)
            bitbuffer_printf(bitbuffer, "wt450_callback: wrong parity (%x)\n", parity);
        return DECODE_FAIL_MIC;
    }

    house_code    = b[0] & 0xF;
    channel       = (b[1] >> 6) + 1;
    battery_low   = b[1] & 0x8;
    humidity      = ((b[1] & 0x7) << 4) | (b[2] >> 4);
    temp_whole    = (b[2] << 4) | (b[3] >> 4);
    temp_fraction = (b[3] & 0xF);
    temp          = (temp_whole - 50) + (temp_fraction / 16.0);
    seq           = (b[4] >> 6);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, _X("WT450-TH","WT450 sensor"),
            "id",               "House Code",   DATA_INT,    house_code,
            "channel",          "Channel",      DATA_INT,    channel,
            "battery",          "Battery",      DATA_STRING, battery_low ? "LOW" : "OK",
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "seq",              "Sequence",     DATA_INT,    seq,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "battery",
        "temperature_C",
        "humidity",
        "seq",
        NULL,
};

r_device wt450 = {
        .name        = "WT450, WT260H, WT405H",
        .modulation  = OOK_PULSE_DMC,
        .short_width = 976,  // half-bit width 976 us
        .long_width  = 1952, // bit width 1952 us
        .reset_limit = 18000,
        .tolerance   = 100, // us
        .decode_fn   = &wt450_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
