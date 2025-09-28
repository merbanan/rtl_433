/** @file
    KIA Pacific HKMC TPMS data.

    File created by modifying the tpms_hyundai_vdo.c.

    Copyright (C) 2025 Ondrej Sychrovsky.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
KIA Pacific HKMC TPMS data.

Tested on a KIA Ceed 2023. The sensors used are not original, probably coded for Kia.
They identify themselves as Pacific HKMC.

It seems that the sensor uses FSK modulation, sends the preamble 55 55 56 and then makes a 3-bit gap.
When the gap is coded into the decoder a new line of data is received, which I do not know how to detect.
The decoder is therefore configured without gap and the gap is accounted for by shifting the data after removing
the preamble. That is the reason for + 27 instead of + 24 in the code below.
After removing the preamble, the Differential Manchester decoding is run on the remaining data:

For my four sensors I have obtained the following message:

90 id id id PP xx xx xx xx xx

where
- id id id  = ID of the sensor. I was able to match this ID to the ID that was shown on a dedicated programming
              device for all four sensors.
- PP        = Pressure with an experimentally set compute formula of p_kPa = PP * 3.2282 - 448.706

I was not able to locate the temperature, which this sensor is also sending.

I was not able to locate any CRC, I have seen transmissions differ only in the last nibble, which it should not
if that were any kind of CRC.

There is also no repeat counter, I have seen e.g. 10 identical messages at quick succession.

The main purpose of this code is only to intercept the message from the sensors. Decoding it is only the icing.
The reason for this is that at higher speeds, the sensors seem to stop transmitting. I needed to verify it.

*/

#include "decoder.h"

static int tpms_pacific_hkmc_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    int state;
    unsigned id;
    int pressure;

    bitbuffer_differential_manchester_decode(bitbuffer,row,bitpos,&packet_bits,80);

    if (packet_bits.bits_per_row[0] < 80) {
        return DECODE_FAIL_SANITY; // too short to be a whole packet
    }

    b = packet_bits.bb[0];

    state         = b[0];
    id            = (unsigned) (b[0] & 0x0f) << 24 | b[1] << 16 | b[2] << 8 | b[3];
    pressure      = b[4];

    char id_str[8 + 1];
    snprintf(id_str, sizeof(id_str), "%07x", id);

    char data_str[13+1];
    snprintf(data_str, sizeof(data_str), "%02x%02x%02x%02x%02x%02x", b[4], b[5], b[6], b[7], b[8], b[9] );

    char state_str[3];
    snprintf(state_str,sizeof(state_str), "%02x", state );

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Pacific HKMC",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "state",            "",             DATA_STRING, state_str,
            "pressure_kPa",     "pressure",     DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure * 3.2282 - 448.706,
            "data",             "",             DATA_STRING, data_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
Wrapper for the Kia Pacific HKMC tpms.
@sa tpms_pacific_hkmc_decode()
*/
static int tpms_pacific_hkmc_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is 55 55 56
    uint8_t const preamble_pattern[3] = {0x55, 0x55, 0x56};

    unsigned bitpos = 0;
    int ret         = 0;
    int events      = 0;

    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 24)) + 83 <=
            bitbuffer->bits_per_row[0]) {
        // See description for why "+ 27" instead of "+ 24"
        ret = tpms_pacific_hkmc_decode(decoder, bitbuffer, 0, bitpos + 27);
        if (ret > 0)
            events += ret;
        bitpos += 2;
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "state",
        "pressure_kPa",
        "data",
        NULL,
};

r_device const tpms_pacific_hkmc = {
        .name        = "Kia TPMS (Pacific HKMC)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,
        .long_width  = 52,
        .reset_limit = 500,
        .decode_fn   = &tpms_pacific_hkmc_callback,
        .fields      = output_fields,
};
