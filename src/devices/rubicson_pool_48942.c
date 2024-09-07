/** @file
    Rubicson pool thermometer 48942 decoder.

    Copyright (C) 2022 Robert Högberg

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Rubicson pool thermometer 48942 decoder.

The device uses OOK and fixed period PWM.
- 0 is encoded as 240 us pulse and 480 us gap,
- 1 is encoded as 480 us pulse and 240 us gap.

A transmission consists of an initial preamble followed by sync
pulses and the data. Sync pulses and data are sent twice.

Preamble:
     __      ____      ____      ____      ____
    |  |____|    |____|    |____|    |____|    |__________
    480 980  980  980  980  980  980  980  980  3880     [us]

Sync pulses:
     ___     ___     ___     ___
    |   |___|   |___|   |___|   |___
    730  730 730 730 730 730 730 730    [us]

The device's transmission interval depends on the configured
channel. The interval is 55 + `device channel` seconds.

Data format:
    71       ba       4e       60       ba       0
    01110001 10111010 01001110 01100000 10111010 0
    CCCCRRRR RRRRRR10 BTTTTTTT TTTT0000 XXXXXXXX 0

- C: channel - offset by 1; 0000 means channel 1
               The device can be configured to use channels 1-8
- R: random power on id
- 1: constant 1
- 0: constant 0
- B: low battery indicator
- T: temperature - offset by 1024 and scaled by 10
- X: CRC
*/

#include "decoder.h"

static int rubicson_pool_48942_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 41);
    if (row < 0 || bitbuffer->bits_per_row[row] != 41)
        return DECODE_ABORT_LENGTH;

    uint8_t *b = bitbuffer->bb[row];
    bitbuffer_invert(bitbuffer);

    // validate some static bits
    if (b[3] & 0xF || b[5])
        return DECODE_ABORT_EARLY;

    if (crc8(b, 4, 0x31, 0x00) != b[4])
        return DECODE_FAIL_MIC;

    int channel = (b[0] >> 4) + 1;
    int random_id = (b[0] & 0x0F) << 6 | (b[1] & 0xFC) >> 2;
    int battery_low = b[2] >> 7;
    float temp_c = ((((b[2] & 0x7F) << 4) | (b[3] >> 4)) - 1024) * 0.1f;

    decoder_log_bitbuffer(decoder, 1, __func__, bitbuffer, "");

    /* clang-format off */
    data_t *data = data_make(
            "model",          "",             DATA_STRING,  "Rubicson-48942",
            "channel",        "Channel",      DATA_INT,     channel,
            "id",             "Random ID",    DATA_INT,     random_id,
            "battery_ok",     "Battery",      DATA_INT,     !battery_low,
            "temperature_C",  "Temperature",  DATA_FORMAT,  "%.1f C", DATA_DOUBLE, temp_c,
            "mic",            "Integrity",    DATA_STRING,  "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "channel",
        "id",
        "battery_ok",
        "temperature_C",
        "mic",
        NULL,
};

r_device const rubicson_pool_48942 = {
        .name        = "Rubicson Pool Thermometer 48942",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 280,
        .long_width  = 480,
        .reset_limit = 6000,
        .gap_limit   = 5000,
        .sync_width  = 730,
        .decode_fn   = &rubicson_pool_48942_decode,
        .fields      = output_fields,
};
