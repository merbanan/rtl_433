/** @file
    Revolt NC5462 Energy Meter.

    Copyright (C) 2023 Nicolai Hess

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**

Revolt energy meter (NC5462)
Sends on 433.92 MHz.

Pulse Width Modulation with startbit/delimiter

Two Modes:
Normal data mode:
105 pulses
first pulse sync
104 data pulse (11 times 8 bit data + 8 bit checksum + 8 bit unknown)
11 byte data:

| data         | byte      |
|--------------|-----------|
| id           | 0,1       |
| voltage      | 2         |
| current      | 3,4       |
| frequency    | 5         |
| power        | 6,7       |
| power factor | 8         |
| energy       | 9,10      |
| detect flag  | first bit |


"Register" mode (after pushing button on energy meter):

same 104 data pulses as in data mode, but first bit high and multiple rows of (the same) data.

Pulses
 - sync ~ 10 ms high / 280 us low
 - 1-bit ~ 320 us high / 160 us low
 - 0-bit ~ 180 us high / 160 us low
message end 180 us high / 100 ms low

rtl_433 demodulation output
(normal data)
short_width: 200, long_width: 330, reset_limit: 240, sync_width: 10044
(detect flag)
short_width: 200, long_width: 330, reset_limit: 240, sync_width: 10044

*/

#include "decoder.h"

static int revolt_nc5462_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *bb = bitbuffer->bb[0];
    data_t *data;
    uint16_t id;
    uint8_t voltage;
    uint16_t current;
    uint8_t frequency;
    uint16_t power;
    uint8_t pf;
    uint16_t energy;
    uint8_t detect_flag;
    uint8_t checksum;
    uint8_t byte12;
    int index;

    bitbuffer_invert(bitbuffer);

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }
    if (bitbuffer->bits_per_row[0] != 104) {
        return DECODE_ABORT_EARLY;
    }
    checksum = 0;
    byte12 = bb[11];
    for (index=0; index<11;++index) {
        checksum+=bb[index];
    }
    if (checksum != byte12) {
        return DECODE_FAIL_MIC;
    }
    id = (bb[0]<<8) | (bb[1]);
    if ((id & 0x8000) == 0x8000) {
        detect_flag = 1;
        id &= ~0x8000;
    } 
    else {
        detect_flag = 0;
    }

    voltage = bb[2];
    current = bb[4] | bb[3]<<8;
    frequency = bb[5];
    power = bb[7] | bb[6]<<8;
    pf = bb[8];
    energy = bb[10] | bb[9]<<8;
    /* clang-format off */
    data = data_make(
            "model",           "",             DATA_STRING, "NC5462",
            "id",              "House Code",   DATA_INT,    id,
            "voltage_V",       "Voltage",      DATA_FORMAT, "%d V",      DATA_INT, voltage,
            "current_A",       "Current",      DATA_FORMAT, "%.02f A",   DATA_DOUBLE, current * 0.01,
            "frequency_Hz",    "Frequency",    DATA_FORMAT, "%d Hz",     DATA_INT, frequency,
            "power_W",         "Power",        DATA_FORMAT, "%.02f W",   DATA_DOUBLE, power * 0.1,
            "power_factor_VA", "Power factor", DATA_FORMAT, "%.02f VA",  DATA_DOUBLE, pf * 0.01,
            "energy_kWh",      "Energy",       DATA_FORMAT, "%.02f kWh", DATA_DOUBLE, energy * 0.01,
            "detect_flag",     "Detect Flag",  DATA_STRING, detect_flag ? "Yes" : "No",
            "mic",             "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);
    return 1;
}

static char* output_fields[] = {
        "model",
        "id",
        "voltage_V",
        "current_A",
        "frequency_Hz",
        "power_W",
        "power_factor_VA",
        "energy_kWh",
        "detect_flag",
        "mic",
        NULL
};


r_device const revolt_nc5462 = {
        .name           = "Revolt NC-5642",
        .modulation     = OOK_PULSE_PWM,
        .short_width    = 200,
        .long_width     = 320,
        .sync_width     = 10024,
        .reset_limit    = 272,
        .decode_fn      = &revolt_nc5462_decode,
        .fields         = output_fields,
};
