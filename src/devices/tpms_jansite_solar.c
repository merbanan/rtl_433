/** @file
    Jansite FSK 7 byte Manchester encoded checksummed TPMS data.

    Copyright (C) 2019 Andreas Spiess and Christian W. Zuckschwerdt <zany@triq.net> and Benjamin Larsson 2021

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Jansite Solar TPMS Solar Model

Data layout (nibbles):

    II II II IS PP TT CC

- I: 28 bit ID
- S: 4 bit Status (deflation alarm, battery low etc)
- P: 8 bit Pressure (best guess quarter PSI, i.e. ~0.58 kPa)
- T: 8 bit Temperature (deg. C offset by 50)
- C: 8 bit Checksum
- The preamble is 0xaa..aa9 (or 0x55..556 depending on polarity)
*/

#include "decoder.h"

static int tpms_jansite_solar_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    data_t *data;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    unsigned id;
    char id_str[7 + 1];
    int flags;
    int pressure;
    int bar;
    int temperature;
    char code_str[9 * 2 + 1];

    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 88);
    bitbuffer_invert(&packet_bits);
//    bitbuffer_debug(&packet_bits);
    
    if (packet_bits.bits_per_row[0] < 88) {
        return DECODE_FAIL_SANITY;
        // fprintf(stderr, "%s packet_bits.bits_per_row = %d\n", __func__, packet_bits.bits_per_row[0]);
    }
    b = packet_bits.bb[0];
    
    if ((b[0]<<8 | b[1]) != 0xdd33) {
        fprintf(stdout, "0x%X\n",b[0]<<8 | b[1]);
        return DECODE_FAIL_SANITY;
    }

    // TODO: validate checksum

    id          = (unsigned)b[2] << 16 | b[3] << 8 | b[4];
    flags       = b[5];
    temperature = b[6];
    pressure    = b[7];
    bar         = b[7];
    //crc         = b[6];
    sprintf(id_str, "%06x", id);
    sprintf(code_str, "%02x%02x%02x%02x%02x%02x%02x%02x%02x", b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10]); // figure out the checksum

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Jansite Solar",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "flags",            "",             DATA_INT, flags,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (float)pressure * 1.6,
            "bar_int",          "",             DATA_INT, bar,
            "bar",              "",             DATA_FORMAT, "%.3f", DATA_DOUBLE, (float)bar*0.01565,
            "bar_round",              "",       DATA_FORMAT, "%.1f", DATA_DOUBLE, (float)bar*0.016,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C", DATA_DOUBLE, (float)temperature - 55.0,
            "code",             "",             DATA_STRING, code_str,
            //"mic",              "",             DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_jansite_decode() */
static int tpms_jansite_solar_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is
    // 0101 0101  0101 0101  0101 0101  0101 0110 = 55 55 55 56
    uint8_t const preamble_pattern[3] = {0xa6, 0xa6, 0x5a}; // after invert

    unsigned bitpos = 0;
    int ret         = 0;
    int events      = 0;

//    bitbuffer_debug(bitbuffer);
    bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 24);
//    fprintf(stdout, "%d \n", bitpos);
    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 24)) + 80 <=
            bitbuffer->bits_per_row[0]) {
//        bitbuffer_debug(bitbuffer);
        ret = tpms_jansite_solar_decode(decoder, bitbuffer, 0, bitpos);
        if (ret > 0)
            events += ret;
        bitpos += 2;
    }

    return events > 0 ? events : ret;
}

static char *output_fields[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_kPa",
        "temperature_C",
        "code",
        //"mic",
        NULL,
};

r_device tpms_jansite_solar = {
        .name        = "Jansite TPMS Model Solar",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 51,  // 12-13 samples @250k
        .long_width  = 51,  // FSK
        .reset_limit = 150, // Maximum gap size before End Of Message [us].
        .decode_fn   = &tpms_jansite_solar_callback,
        .disabled    = 1, // Unknown checksum
        .fields      = output_fields,
};
