/** @file
    Decoder for Digitech Tech-433 temperature sensor.

	manufacturer: Atech

	model name: Atech wireless weather station (presumed name, but not on the device, WS-308). On the outdoor sensor it says: 433 tech remote sensor

	information and photo:https://www.gitmemory.com/issue/RFD-FHEM/RFFHEM/547/474374179 

    Copyright (C) 2020 Marc Prieur https://github.com/marco402

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

The encoding is pulse position modulation
(i.e. gap width contains the modulation information)

- pulse high short gap
- pulse low long gap

I use modulation type OOK_PULSE_PPM_spe 

	first invert all bits
	second decode as this:
		00-->0
		01-->1
		11-->nothing
		10-->nothing
	third
		put only 4 lsb in the byte with shift 1 bit.


A transmission package is:
	-preambule 8 "1"
	-very long gap
	-four identiques paquets 46 bits(if we count the last bit)

-this code display 4 paquets.

-after treatment
- byte 0:   0000 
- byte 1: preamble (for synchronisation),  1100   
- byte 2:signe 3°bit
- byte 3 centaines
- byte 4 dizaines
- byte 5 unités
- byte 6 a check byte (the XOR of bytes 1-6 inclusive)
    each bit is effectively a parity bit for correspondingly positioned bit
    in the real message
exemple:
00001101100000000001101100000110000001101
00->0
 00->0
  00->0
   01->1
    11->nothing
     10->nothing
      01->1...

000 1100 0000  0001  1000 0100 0000    1
    c    signe 1*100 8*10 4    parite
=184/10 degres

hexa 0c   00   01   08   04   00
     b[1] b[2] b[3] b[4] b[5] b[6]

for coding
0->0
1->011

so we can decode also as this

0->0
011->1
*/

#include "decoder.h"

static int tech_433_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int first_byte,  temp_raw; 
    float temp_c;

    int r = bitbuffer_find_repeated_row(bitbuffer, 1, 28);
    if (r < 0)
        return DECODE_ABORT_EARLY;

	b= (bitbuffer->bb[r]);
         if (bitbuffer->bits_per_row[r] != 64 )
        return DECODE_ABORT_LENGTH;

    //if (b[1] != 0x0c)
    //    return DECODE_FAIL_SANITY;

    if ((b[1] ^ b[2] ^ b[3] ^ b[4] ^ b[5] ^ b[6]) != 0)
        return DECODE_FAIL_MIC;

    first_byte = b[1];
    if (b[2] & 2)
		temp_c   =(float) -(((b[3] * 100) + (b[4] * 10) + b[5]) / 10.0);
    else
		temp_c   =(float) (((b[3] * 100) + (b[4] * 10) + b[5]) / 10.0);

    data = data_make(
            "model",            "",             DATA_STRING, _X("tech_433","Tech 433"),
            "id",               "First byte",           DATA_INT,    first_byte,
           "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        //"channel",  ???
        //"battery",  ???
        "temperature_C",
        "mic",
        NULL,
};

r_device tech_433 = {
        .name        = "tech_433",
        .modulation  = OOK_PULSE_PPM_SPE,
        .short_width = 240,  // short gap    "1" 
        .long_width  = 1950, // long gap     "0"
        .gap_limit   = 8000, // packet gap
        .reset_limit = 10000,
        .tolerance   = 180,
        .decode_fn   = &tech_433_callback,
        .fields      = output_fields,
};
