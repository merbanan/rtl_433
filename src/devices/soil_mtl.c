/** @file
    Soil Moisture/Temp/Light level sensor decoder.

    Copyright (C) 2025 Boing <dhs.mobil@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Soil Moisture/Temp/Light level sensor decoder.

Known devices:
- Geevon T23033 / T230302 Soil Moisture/Temp/Light Level Sensor, ASIN B0D9Z9HLYD
  see #2977 by emmjaibi for excellent analysis

also:
- Dr.Meter, 1PCS sensor only, ASIN B0CQKYTBC6
- Royal Gardineer ZX8859-944, ASIN B0DQTYYZK8
- some unbranded sensors on AliEexpress

Wireless 433 MHz in EU region, unidirectional.
Modulation OOK PWM with 400/1200 us timing, inverted bits.

Example code:
    raw      {65}55aaee8ddae84fcf
    preamble {16}0x55aa
    inverted {65}aa5513fd001630800

# Data Layout

        PPPP PPPP PPPP PPPP IIII IIII IIII IIII MMMM MMMM STTT TTTT QQBB LLLL CCCC SSSSSSSS

- P = Preamble of 16 bits equal to 0xaa55 (inverted)
- I =ID 16 bits length seems to survive battery changes
- M = soil moisture ~0-99% as an 8 bit integer
- S = sign for temperature (0 for positive or 1 for negative)
- T = Temperature as 7bit integer ~0-100C
- Q = 2 sequence bits
  - device sends message on CHS change !
  - sequence:
  - S 00  initial phase duration 150s
  - S 01  intervall timer 3min
  - S 02  intervall timer 15min
  - S 03  intervall timer 30min
- B = battery status in values of 1-3 for number of segments to show for battery
- L = light level (9 states from LOW- to HIGH+)
- C = 4 bit checksum
- S = Sync of 8 bits equal to 0xf8 , can be ignored
- 9 repeats

Note: Device drifts in direct sun and shows up to 12C offset.
Note: Device is NOT waterproof (IP27),  don't immerse in water.
Note: Uses one AA battery AA or rechargeable cell, lasts for up to: 18 months.
*/
static int soil_mtl_decoder(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Check that num_rows is 1
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY; // wrong num_rows
    }
    int row = 0;
    // Check that bits_per_row is 65
    unsigned row_len = bitbuffer->bits_per_row[row]; // 65, last one would be 61
    if (row_len != 65) {
        return DECODE_ABORT_EARLY; // wrong Data Length (must be 65)
    }

    // Search preamble
    uint8_t const preamble[] = {0x55, 0xaa};
    unsigned pos = bitbuffer_search(bitbuffer, 0, 0, preamble, 16);
    if (pos > 8) { // match only near the start
        return DECODE_ABORT_LENGTH; // preamble not found
    }

    // Invert data
    bitbuffer_invert(bitbuffer);

    uint8_t *b = bitbuffer->bb[row];

    // Nibble-wide checksum validation
    uint8_t chs = (b[7] & 0xf0) >> 4;
    uint8_t c_sum = add_nibbles(b, 7) & 0x0f;

    if (c_sum != chs) {
        return DECODE_FAIL_MIC; // Checksum fault
    }

    int id          = (b[2] << 8) | b[3];
    int moisture    = b[4];
    int t_sign      = (b[5] & 0x80) >> 7;
    int temperature = b[5] & 0x7f;
    int sequence    = (b[6] & 0xc0) >> 6;
    int batt_lvl    = (b[6] & 0x30) >> 4;
    int light_lvl   = (b[6] & 0x0f);

    if (t_sign) {
        temperature = (0 - temperature);
    }

    /* clang-format off */
    data_t *data = data_make(
    		"model",            "Model",            DATA_STRING,    "Soil-MTL",
    		"id",               "ID",               DATA_FORMAT,    "0x%02X",    DATA_INT,    id,
    		"moisture_pct",     "Moisture Pct",     DATA_INT,       moisture,
    		"temperature_C",	"Temperature C",	DATA_INT,       temperature,
    		"sequence",         "Sequence",         DATA_FORMAT,    "0x%02X",    DATA_INT,    sequence,
    		"battery_lvl",      "Batt level",       DATA_INT,       batt_lvl,
    		"light_lvl",        "Light level",      DATA_INT,       light_lvl,
    		"mic",              "Integrity",        DATA_STRING,	"CHECKSUM",
    		NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    bitbuffer_invert(bitbuffer); // FIXME: DEBUG, remove this
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "moisture_pct",
        "temperature_C",
        "sequence",
        "battery_lvl",
        "light_lvl",
        "mic",
        NULL,
};

r_device const soil_mtl = {
        .name        = "Soil Moisture/Temp/Light level sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 432,  // 400
        .long_width  = 1228, // 1200
        .gap_limit   = 2000,
        .reset_limit = 3000,
        .decode_fn   = &soil_mtl_decoder,
        .fields      = output_fields,
};
