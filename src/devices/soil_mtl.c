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

Example code:
    raw      {65}55aaee8ddae84fcf
    preamble {16}0x55aa
    inverted {65}aa5513fd001630800

# Data transmission

9 repeats of 433.92 MHz (EU region).
Modulation is OOK PWM with 400/1200 us timing, inverted bits.

# Data Layout

        PPPP PPPP PPPP PPPP IIII IIII IIII IIII MMMM MMMM STTT TTTT QQBB LLLL CCCC XXXXXXXX

- P = Preamble of 16 bits with 0xaa55 (inverted)
- I = ID 16 bits, seems to survive battery changes
- M = soil moisture 0-100% as an 8 bit integer
- S = sign for temperature (0 for positive or 1 for negative)
- T = Temperature as 7 bit integer ~0-100C
- Q = 2 sequence bits
  - device sends message on CHS change !
  - sequence:
  - S 00  initial phase duration 150 secs
  - S 01  interval timer 3 mins
  - S 02  interval timer 15 mins
  - S 03  interval timer 30 mins
- B = battery status of 1 (1.22 V) to 3 (above 1.42 V), 0 so far has not been observed?
- L = light level (9 states from LOW- to HIGH+)
  - 0 (LOW-)     0
  - 1 (LOW)    > 120 Lux
  - 2 (LOW+)   > 250 Lux
  - 3 (NOR-)   > 480 Lux
  - 4 (NOR)    > 750 Lux
  - 5 (NOR+)   >1200 Lux
  - 6 (HIGH-)  >1700 Lux
  - 7 (HIGH)   >3800 Lux
  - 8 (HIGH+)  >5200 Lux, max 15000 Lux for Dr.meter
- C = 4 bit checksum
- X = Trailer of 8 bits equal to 0xf8 , can be ignored

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
    int chk = (b[7] & 0xf0) >> 4;
    int sum = add_nibbles(b, 7) & 0x0f;

    if (sum != chk) {
        return DECODE_FAIL_MIC; // Checksum mismatch
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
    		"id",               "ID",               DATA_FORMAT,    "%04X",	 	 DATA_INT,    id,
            "battery_ok",       "Battery",      	DATA_INT,    	batt_lvl > 1, // Level 1 means "Low"
    		"battery_pct",      "Battery level",    DATA_INT,       100 * batt_lvl / 3, // Note: this might change with #3103
    		"temperature_C",	"Temperature C",	DATA_INT,       temperature,
            "moisture",         "Moisture",     	DATA_FORMAT, 	"%d %%", 	 DATA_INT, moisture,
    		"light_lvl",        "Light level",      DATA_INT,       light_lvl,
    		"sequence",         "TX Sequence",      DATA_FORMAT,    "0x%02X",    DATA_INT,    sequence,
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
        "battery_ok",
        "battery_pct",
        "temperature_C",
        "moisture",
        "light_lvl",
        "sequence",
        "mic",
        NULL,
};

r_device const soil_mtl = {
        .name        = "Geevon, Dr.Meter, Royal Gardineer Soil Moisture/Temp/Light sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 432,  // 400
        .long_width  = 1228, // 1200
        .gap_limit   = 2000,
        .reset_limit = 3000,
        .decode_fn   = &soil_mtl_decoder,
        .fields      = output_fields,
};
