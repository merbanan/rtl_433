/** @file
    Companion WTR001 Temperature Sensor decoder.

    Copyright (C) 2019 Karl Lohner <klohner@thespill.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/**
Companion WTR001 Temperature Sensor decoder.

The device uses PWM encoding with 2928 us for each pulse plus gap.
- Logical 0 is encoded as 732 us pulse and 2196 us gap,
- Logical 1 is encoded as 2196 us pulse and 732 us gap,
- SYNC is encoded as 1464 us and 1464 us gap.

A transmission starts with the SYNC,
there are 5 repeated packets, each ending with a SYNC.

Full message is (1+5*(14+1))*2928 us = 304*2928us = 890,112 us.
Final 1464 us is gap silence, though.

E.g. rtl_433 -R 0 -X 'n=WTR001,m=OOK_PWM,s=732,l=2196,y=1464,r=2928,bits>=14,invert'

Data layout (14 bits):

    DDDDDXTT TTTTTP

| Ordered Bits     | Description
|------------------|-------------
| 4,3,2,1,0        | DDDDD: Fractional part of Temperature. (DDDDD - 10) / 10
| 5                | X: Always 0 in testing. Maybe battery_OK or fixed
| 12,7,6,11,10,9,8 | TTTTTTT: Temperature in Celsius = (TTTTTTT + ((DDDDD - 10) / 10)) - 41
| 13               | P: Parity to ensure count of set bits in data is odd.

Temperature in Celsius = (bin2dec(bits 12,7,6,11,10,9,8) + ((bin2dec(bits 4,3,2,1,0) - 10) / 10 ) - 41

Published range of device is -29.9C to 69.9C
*/

#include "decoder.h"

#define MYDEVICE_BITLEN      14
#define MYDEVICE_MINREPEATS  3

static int companion_wtr001_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{

    data_t *data;
    int r; // a row index
    uint8_t b[2];
    float temperature;

    r = bitbuffer_find_repeated_row(bitbuffer, MYDEVICE_MINREPEATS, MYDEVICE_BITLEN);
    if (r < 0 || bitbuffer->bits_per_row[r] != 14) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, r, 0, b, 14);

    // Invert these 14 bits, PWM with short pulse is 0, long pulse is 1
    b[0] = ~b[0];
    b[1] = ~b[1] & 0xfc;

    // Make sure bit 5 is not set
    if ((b[0] & 0x04) == 0x04) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "companion_wtr001: Fixed Bit set (and it shouldn't be)\n");
        }
        return DECODE_FAIL_SANITY;
    }

    /* Parity check (must be ODD) */
    if (!parity_bytes(b, 2)) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "companion_wtr001: parity check failed (should be ODD)\n");
        }
        return DECODE_FAIL_MIC;
    }

    // Get the tenth of a degree C as bits 0,1,2,3,4 reversed, minus 0x0a
    // bin2dec(bits 4,3,2,1,0) - 10)
    uint8_t temp_tenth_raw = reverse8(b[0] & 0xf8);

    if (temp_tenth_raw < 0x0a) {
        // Value is too low
        if (decoder->verbose > 1) {
            fprintf(stderr, "companion_wtr001: Temperature Degree Tenth too low (%d - 10 is less than 0\n", temp_tenth_raw);
        }
        return DECODE_FAIL_SANITY;
    }

    if (temp_tenth_raw > 0x13) {
        // Value is too high
        if (decoder->verbose > 1) {
            fprintf(stderr, "companion_wtr001: Temperature Degree Tenth too high (%d - 10 is greater than 9\n", temp_tenth_raw);
        }
        return DECODE_FAIL_SANITY;
    }

    temp_tenth_raw -= 0x0a;

    // Shift these 7 bits around into the right order
    // bin2dec(bits 12,7,6,11,10,9,8)
    uint8_t temp_whole_raw = reverse8(b[1]&0xf0) | reverse8(b[0]&0x03)>>2 | (b[1]&0x08)<<3;

    if (temp_whole_raw < 11) {
        // Value is too low (outside published specs)
        if (decoder->verbose > 1) {
            fprintf(stderr, "companion_wtr001: Whole part of Temperature is too low (%d - 41 is less than -30)\n", temp_whole_raw);
        }
        return DECODE_FAIL_SANITY;
    }

    if (temp_whole_raw > 111) {
        // Value is too high (outside published specs)
        if (decoder->verbose > 1) {
            fprintf(stderr, "companion_wtr001: Whole part of Temperature is too high (%d - 41 is greater than 70)\n", temp_whole_raw);
        }
        return DECODE_FAIL_SANITY;
    }

    // Add whole temperature part to (tenth temperature part / 10), then subtract 41 for final (float) temperature reading
    temperature = (temp_whole_raw + (temp_tenth_raw * 0.1f)) - 41.0f;

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, "Companion-WTR001",
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f", DATA_DOUBLE, temperature,
            "mic",           "Integrity",   DATA_STRING, "PARITY",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
        "model",
        "temperature_C",
        "mic",
        NULL,
};

r_device companion_wtr001 = {
        .name        = "Companion WTR001 Temperature Sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 732,  // 732 us pulse + 2196 us gap is 1 (will be inverted in code)
        .long_width  = 2196, // 2196 us pulse + 732 us gap is 0 (will be inverted in code)
        .gap_limit   = 4000, // max gap is 2928 us
        .reset_limit = 8000, //
        .sync_width  = 1464, // 1464 us pulse + 1464 us gap between each row
        .decode_fn   = &companion_wtr001_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
