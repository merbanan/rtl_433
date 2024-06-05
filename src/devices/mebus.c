/** @file
    Mebus 433.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Mebus 433.

@todo Documentation needed.
*/
static int mebus433_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitrow_t *bb = bitbuffer->bb;
    int16_t temp;
    int8_t hum;
    uint8_t address;
    uint8_t channel;
    uint8_t battery;
    uint8_t unknown1;
    uint8_t unknown2;
    data_t *data;

    // TODO: missing packet length validation

    if (bb[0][0] == 0 && bb[1][4] !=0 && (bb[1][0] & 0x60) && bb[1][3]==bb[5][3] && bb[1][4] == bb[12][4]) {

        address = bb[1][0] & 0x1f;

        channel = ((bb[1][1] & 0x30) >> 4) + 1;
        // Always 0?
        unknown1 = (bb[1][1] & 0x40) >> 6;
        battery  = bb[1][1] & 0x80;

        // Upper 4 bits are stored in nibble 1, lower 8 bits are stored in nibble 2
        // upper 4 bits of nibble 1 are reserved for other usages.
        temp = (int16_t)((uint16_t)(bb[1][1] << 12) | bb[1][2] << 4);
        temp = temp >> 4;
        // lower 4 bits of nibble 3 and upper 4 bits of nibble 4 contains
        // humidity as decimal value
        hum = (bb[1][3] << 4 | bb[1][4] >> 4);

        // Always 0b1111?
        unknown2 = (bb[1][3] & 0xf0) >> 4;

        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Mebus-433",
                "id",               "Address",      DATA_INT,    address,
                "channel",          "Channel",      DATA_INT,    channel,
                "battery_ok",       "Battery",      DATA_INT,    !!battery,
                "unknown1",         "Unknown 1",    DATA_INT,    unknown1,
                "unknown2",         "Unknown 2",    DATA_INT,    unknown2,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp * 0.1f,
                "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, hum,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return DECODE_ABORT_EARLY;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "unknown1",
        "unknown2",
        "temperature_C",
        "humidity",
        NULL,
};

r_device const mebus433 = {
        .name        = "Mebus 433",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 800,  // guessed, no samples available
        .long_width  = 1600, // guessed, no samples available
        .gap_limit   = 2400,
        .reset_limit = 6000,
        .decode_fn   = &mebus433_decode,
        .disabled    = 1, // add docs, tests, false positive checks and then re-enable
        .fields      = output_fields,
};
