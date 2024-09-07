/** @file
    Revolt NC-5462 Energy Meter.

    Copyright (C) 2023 Nicolai Hess

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/

#include "decoder.h"

/**
Revolt NC-5462 Energy Meter.

- Sends on 433.92 MHz.
- Pulse Width Modulation with startbit/delimiter

Two Modes:

## Normal data mode:

- 105 pulses
- first pulse sync
- 104 data pulse (11 times 8 bit data + 8 bit checksum + 8 bit unknown)
- 11 byte data:

| data         | byte      |
|--------------|-----------|
| detect flag  | first bit |
| id           | 0,1       |
| voltage      | 2         |
| current      | 3,4       |
| frequency    | 5         |
| power        | 6,7       |
| power factor | 8         |
| energy       | 9,10      |


## "Register" mode (after pushing button on energy meter):

Same 104 data pulses as in data mode, but first bit high and multiple rows of (the same) data.

Pulses
- sync ~ 10 ms high / 280 us low
- 1-bit ~ 320 us high / 160 us low
- 0-bit ~ 180 us high / 160 us low
- message end 180 us high / 100 ms low

*/

static int revolt_nc5462_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[0] != 104) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *b = bitbuffer->bb[0];

    int sum = add_bytes(b, 11);
    if (sum == 0) {
        return DECODE_FAIL_SANITY;
    }
    int chk = b[11];
    if ((sum & 0xff) != chk) {
        return DECODE_FAIL_MIC;
    }

    int button    = b[0] >> 7;
    int id        = ((b[0] & 0x7f) << 8) | (b[1]);
    int voltage   = b[2];
    int current   = b[4] | b[3] << 8;
    int frequency = b[5];
    int power     = b[7] | b[6] << 8;
    int pf        = b[8];
    int energy    = b[10] | b[9] << 8;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Revolt-NC5462",
            "id",               "House Code",   DATA_INT,    id,
            "voltage_V",        "Voltage",      DATA_FORMAT, "%d V",        DATA_INT,    voltage,
            "current_A",        "Current",      DATA_FORMAT, "%.2f A",      DATA_DOUBLE, current * 0.01,
            "frequency_Hz",     "Frequency",    DATA_FORMAT, "%d Hz",       DATA_INT,    frequency,
            "power_W",          "Power",        DATA_FORMAT, "%.2f W",      DATA_DOUBLE, power * 0.1,
            "power_factor_VA",  "Power factor", DATA_FORMAT, "%.2f VA",     DATA_DOUBLE, pf * 0.01,
            "energy_kWh",       "Energy",       DATA_FORMAT, "%.2f kWh",    DATA_DOUBLE, energy * 0.01,
            "button",           "Button",       DATA_INT, button,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "voltage_V",
        "current_A",
        "frequency_Hz",
        "power_W",
        "power_factor_VA",
        "energy_kWh",
        "button",
        "mic",
        NULL,
};

r_device const revolt_nc5462 = {
        .name        = "Revolt NC-5642 Energy Meter",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 200,
        .long_width  = 320,
        .sync_width  = 10024,
        .reset_limit = 272,
        .decode_fn   = &revolt_nc5462_decode,
        .fields      = output_fields,
};
