/** @file
    Decoder for Sharp SPC775 weather station.

    Copyright (C) 2020 Daniel Drown

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Decoder for Sharp SPC775 weather station.

- Modulation: FSK PWM
- Frequency: 917.2 MHz
- 3900 us long single frequency preamble signal
- 4800 us 2x high to low transitions
- 725 us per symbol, 225 us high for 0, 425 us high for 1
- ends with 3000 us low, then back to the 2x high/low transitions
- data is repeated 3x per transmission
- 48 bits worth of data
- 8 bits of fixed sync (0xa5)
- 8 bits of ID
- 1 bit of battery state
- 3 bits of "unused"?
- 12 bits of signed 0.1C units
- 8 bits of humidity %
- 8 bits of crc

generic parser version:
rtl_433 -f 917.2M -s 250k -R 0 -X n=sharp,m=FSK_PWM,s=225,l=425,y=4000,g=2900,r=150000,invert,bits=48,preamble={8}a5
*/

#include "decoder.h"

static int sharp_spc775_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xa5};

    data_t *data;
    uint8_t *b, crc_calc;
    uint16_t temp_raw;
    int i, id, humidity, battery_low, crc, crc_expected;
    int r=-1, length_match=0, preamble_match=0;
    float temp_c;
    bitrow_t tmp;

    // Invert data for processing
    bitbuffer_invert(bitbuffer);

    for (i = 0; i < bitbuffer->num_rows; i++) {
        if (bitbuffer->bits_per_row[i] >= 48) {
            length_match++;
            unsigned pos = bitbuffer_search(bitbuffer, i, 0, preamble, sizeof(preamble)*8);
            if (pos < bitbuffer->bits_per_row[i]) {
                if (r < 0)
                    r = i;
                preamble_match++;
                pos += sizeof(preamble)*8;
                unsigned len = bitbuffer->bits_per_row[i] - pos;
                bitbuffer_extract_bytes(bitbuffer, i, pos, tmp, len);
                memcpy(bitbuffer->bb[i], tmp, (len + 7) / 8);
                bitbuffer->bits_per_row[i] = len;
            }
        }
    }

    if (!length_match)
        return DECODE_ABORT_LENGTH;
    if (!preamble_match)
        return DECODE_FAIL_SANITY;

    b = bitbuffer->bb[r];

    id          = b[0];                                                // changes on each power cycle
    battery_low = (b[1] & 0x80);                                       // High bit is low battery indicator
    temp_raw    = (int16_t)(((b[1] & 0x0f) << 12) | (b[2] << 4));      // uses sign-extend
    temp_c      = (temp_raw >> 4) * 0.1f;                              // Convert sign extended int to float
    humidity    = b[3];                                                // Simple 0-100 RH
    crc         = b[4];

    crc_calc = preamble[0];
    for (i = 0; i < 4; i++)
        crc_calc = crc_calc ^ b[i];

    crc_expected = lfsr_digest8_reflect(&crc_calc, 1, 0x31, 0x31);

    if (crc_expected != crc)
        return DECODE_FAIL_MIC;

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "Sharp-SPC775",
            "id",               "",                 DATA_INT,    id,
            "battery_ok",       "",                 DATA_INT,    !battery_low,
            "temperature_C",    "Temperature",      DATA_FORMAT, "%.01f C",  DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",         DATA_FORMAT, "%u %%",    DATA_INT,    humidity,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "humidity",
        NULL,
};

r_device sharp_spc775 = {
        .name        = "Sharp SPC775 weather station",
        .modulation  = FSK_PULSE_PWM,
        .short_width = 225,
        .long_width  = 425,
        .gap_limit   = 2900,
        .reset_limit = 10000,
        .decode_fn   = &sharp_spc775_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
