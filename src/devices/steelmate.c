/** @file
    Steelmate TPMS FSK protocol.

    Copyright (C) 2016 Benjamin Larsson
    Copyright (C) 2016 John Jore
    Copyright (C) 2025 Bruno OCTAU (ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Steelmate TPMS FSK protocol.

Reference:

- model TP-S15

Brand:

- Steelmate
- R-Lake

S.a. issue #3200 Pressure issue :

- The originally guessed formula was : Pressure in PSI scale 2, but more the pressure is important more the value diverged between the TPMS display and rtl_433.
- New analysis : Based on data collected by \@e100 + the technical specification ( 0~7.9Bar ) + analysis by \@e100 and refined by \@ProfBoc75, the pressure is given in Bar at scale 32.

Packet payload:

- 9 bytes.

Bytes 2 to 9 are inverted Manchester with swapped MSB/LSB:

                                  0  1  2  3  4  5  6  7  8
                       [00] {72} 00 00 7f 3c f0 d7 ad 8e fa
    After translating            00 00 01 c3 f0 14 4a 8e a0
                                 SS SS AA II II PP TT BB CC

- S = sync, (0x00)
- A = preamble, (0x01)
- I = id, 0xc3f0
- P = Pressure in Bar, scale 32, 0xA0 / 32 = 5 Bar, or 0xA0 * 3.125 = 500 kPA, see issue #3200
- T = Temperature in Celcius + 50, 0x4a = 24 'C
- B = Battery, where mV = 3900-(value*10). E.g 0x8e becomes 3900-(1420) = 2480mV.
-     This calculation is approximate fit from sample data, any improvements are welcome.
-   > If this field is set to 0xFF, a "fast leak" alarm is triggered.
-   > If this field is set to 0xFE, a "slow leak" alarm is triggered.
- C = Checksum, adding bytes 2 to 7 modulo 256 = byte 8,(0x01+0xc3+0xf0+0x14+0x4a+0x8e) modulus 256 = 0xa0

*/

#include "decoder.h"

static int steelmate_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0x00, 0x00, 0x7f}; // inverted, raw value is 0x5a
    //Loop through each row of data
    for (int row = 0; row < bitbuffer->num_rows; row++) {
        //Payload is inverted Manchester encoded, and reversed MSB/LSB order
        unsigned row_len = bitbuffer->bits_per_row[row];

        //Length must be 72, 73, 208 or 209 bits to be considered a valid packet
        if (row_len != 72 && row_len != 73 && row_len != 209 && row_len != 208)
            continue; // DECODE_ABORT_LENGTH

        //Valid preamble? (Note, the data is still wrong order at this point. Correct pre-amble: 0x00 0x00 0x01)
        unsigned bitpos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, 24);
        if (bitpos > row_len - 72)
            continue; // DECODE_ABORT_EARLY
        bitbuffer_invert(bitbuffer);
        uint8_t b[9];
        bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, 72);

        reflect_bytes(b, 9);

        //Checksum is a sum of all the other values
        if ((add_bytes(&b[2], 6) & 0xFF) != b[8])
            continue; // DECODE_FAIL_MIC

        //Sensor ID
        uint8_t id1 = b[3];
        uint8_t id2 = b[4];

        //Pressure is stored as 32 * the Bar
        uint8_t p1 = b[5];

        //Temperature is sent as degrees Celcius + 50.
        int temp_c = b[6] - 50;

        //Battery voltage is stored as 100*(3.9v-<volt>).
        uint8_t b1     = b[7];
        int battery_mv = 3900 - b1 * 10;

        int sensor_id      = (id1 << 8) | id2;
        float pressure_kpa = p1 * 3.125f;                         // as guessed in #3200

        char sensor_idhex[7];
        snprintf(sensor_idhex, sizeof(sensor_idhex), "0x%04x", sensor_id);

        /* clang-format off */
        data_t *data = data_make(
                "type",             "",             DATA_STRING,  "TPMS",
                "model",            "",             DATA_STRING,  "Steelmate",
                "id",               "",             DATA_STRING,  sensor_idhex,
                "pressure_kPa",     "",             DATA_FORMAT,  "%.0f kPa", DATA_DOUBLE,  pressure_kpa,
                "temperature_C",    "",             DATA_FORMAT,  "%d C",     DATA_INT,     temp_c,
                "battery_mV",       "",             DATA_COND,    b1 < 0xFE,  DATA_INT,     battery_mv,
                "alarm",            "",             DATA_COND,    b1 == 0xFF, DATA_STRING, "fast leak",
                "alarm",            "",             DATA_COND,    b1 == 0xFE, DATA_STRING, "slow leak",
                "mic",              "Integrity",    DATA_STRING,  "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    //Was not a Steelmate TPMS after all
    // TODO: improve return codes by aborting early
    return DECODE_FAIL_SANITY;
}

static char const *const output_fields[] = {
        "type",
        "model",
        "id",
        "pressure_kPa",
        "temperature_C",
        "battery_mV",
        "alarm",
        "mic",
        NULL,
};

r_device const steelmate = {
        .name        = "Steelmate TPMS",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 50,
        .long_width  = 50,
        .reset_limit = 120,
        .decode_fn   = &steelmate_callback,
        .fields      = output_fields,
};
