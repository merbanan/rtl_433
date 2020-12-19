// SwitchDoc Labs SM23 Wireless Soil Moisture Sensor
// December 2020
// Original code from Tommy Vestermark

/** @file

    Fine Offset Electronics sensor protocol.
    Copyright (C) 2017 Tommy Vestermark
    Enhanced (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>
    Added WH51 Soil Moisture Sensor (C) 2019 Marco Di Leo
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"
#include "fatal.h"
#include <stdlib.h>


/*
SwitchDoc Labs SM23  Soil Moisture Sensor
Test decoding with: rtl_433 -f 433920000  -X "n=soil_sensor,m=FSK_PCM,s=58,l=58,t=5,r=5000,g=4000,preamble=aa2dd4"
Data format:
               00 01 02 03 04 05 06 07 08 09 10 11 12 13
aa aa aa 2d d4 51 00 6b 58 6e 7f 24 f8 d2 ff ff ff 3c 28 8
               FF II II II TB YY MM ZA AA XX XX XX CC SS
Sync:     aa aa aa ...
Preamble: 2d d4
FF:       Family code 0x51 (SDL SM23)
IIIIII:   ID (3 bytes)
T:        Transmission period boost: highest 3 bits set to 111 on moisture change and decremented each transmission;
          if T = 0 period is 70 sec, if T > 0 period is 10 sec
B:        Battery voltage: lowest 5 bits are battery voltage * 10 (e.g. 0x0c = 12 = 1.2V). Transmitter works down to 0.7V (0x07)
YY:       ? Fixed: 0x7f
MM:       Moisture percentage 0%-100% (0x00-0x64) MM = (AD - 70) / (450 - 70)
Z:        ? Fixed: leftmost 7 bit 1111 100
AAA:      9 bit AD value MSB byte[07] & 0x01, LSB byte[08]
XXXXXX:   ? Fixed: 0xff 0xff 0xff
CC:       CRC of the preceding 12 bytes (Polynomial 0x31, Initial value 0x00, Input not reflected, Result not reflected)
SS:       Sum of the preceding 13 bytes % 256
See http://www.ecowitt.com/upfile/201904/WH51%20Manual.pdf for relationship between AD and moisture %
Short explanation:
Soil Moisture Percentage = (Moisture AD – 0%AD) / (100%AD – 0%AD) * 100
0%AD = 70
100%AD = 450 (manual states 500, but sensor internal computation are closer to 450)
If sensor-calculated moisture percentage are inaccurate at low/high values, use the AD value and the above formaula
changing 0%AD and 100%AD to cover the full scale from dry to damp
*/

static int switchdoclabs_SM23_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t const preamble[] = {0xAA, 0x2D, 0xD4};
    uint8_t b[14];
    
    unsigned bit_offset;

    // Validate package
    if (bitbuffer->bits_per_row[0] < 120) {  
        return DECODE_ABORT_LENGTH;
    }

        // Find a data package and extract data payload
        bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8;
        printf("bit_offset=%d\n", bit_offset);
        printf("bit_buffer->bits_per_row[0]=%d\n", bitbuffer->bits_per_row[0]);
   if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        if (decoder->verbose)
            bitbuffer_printf(bitbuffer, "SDL_SM23: short package. Header index: %u\n", bit_offset);
        return DECODE_ABORT_LENGTH;
    }
    
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    int i;
    for (i=0; i < 14; i++)
    {
    printf("b[%i]=0x%2x\n", i, b[i]);
    }
    // Verify family code
    if (b[0] != 0x51) {
        if (decoder->verbose)
            fprintf(stderr, "SDL_SM23: Msg family unknown: %2x\n", b[0]);
        return DECODE_ABORT_EARLY;
    }

    // Verify checksum
    if ((add_bytes(b, 13) & 0xff) != b[13]) {
        if (decoder->verbose)
            bitrow_printf(b, sizeof (b) * 8, "SDL_SM23: Checksum error: ");
        return DECODE_FAIL_MIC;
    }

    // Verify crc
    if (crc8(b, 12, 0x31, 0) != b[12]) {
        if (decoder->verbose)
            bitrow_printf(b, sizeof (b) * 8, "SDL_SM23: Bitsum error: ");
        return DECODE_FAIL_MIC;
    }



    // Decode data
    char id[7];
    sprintf(id, "%2x%2x%2x", b[1], b[2], b[3]);
    int boost           = (b[4] & 0xe0) >> 5;
    int battery_mv      = (b[4] & 0x1f) * 100;
    float battery_level = (battery_mv - 700) / 900; // assume 1.6V (100%) to 0.7V (0%) range
    int ad_raw          = (((int)b[7] & 0x01) << 8) | (int)b[8];
    int moisture        = b[6];

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "SwitchDocLabs-SM23",
            "id",               "ID",               DATA_STRING, id,
            "battery_ok",       "Battery level",    DATA_DOUBLE, battery_level,
            "battery_mV",       "Battery",          DATA_FORMAT, "%d mV", DATA_DOUBLE, battery_mv,
            "moisture",         "Moisture",         DATA_FORMAT, "%u %%", DATA_INT, moisture,
            "boost",            "Transmission boost", DATA_INT, boost,
            "ad_raw",           "AD raw",           DATA_INT, ad_raw,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}


static char *output_fields_SM23[] = {
    "model",
    "id",
    "battery",
    "moisture",
    "boost",
    "ad_raw",
    "mic",
    NULL,
};


r_device switchdoclabs_SM23 = {
    .name           = "SwitchDoc Labs SM23 Soil Moisture Sensor",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 58, // Bit width = 58µs (measured across 580 samples / 40 bits / 250 kHz )
    .long_width     = 58, // NRZ encoding (bit width = pulse width)
    //.short_width    = 58, // Bit width = 58µs (measured across 580 samples / 40 bits / 250 kHz )
    //.long_width     = 58, // NRZ encoding (bit width = pulse width)
    .reset_limit    = 5000,
    .decode_fn      = &switchdoclabs_SM23_callback,
    .disabled       = 0,
    .fields         = output_fields_SM23,
};
