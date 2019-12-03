/** @file
    TFA pool temperature sensor.

    Copyright (C) 2015 Alexandre Coffignal

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
TFA pool temperature sensor.

10 24 bits frames

    CCCCIIII IIIITTTT TTTTTTTT DDBF

- C: checksum, sum of nibbles - 1
- I: device id (changing only after reset)
- T: temperature
- D: channel number
- B: battery status
- F: first transmission
*/

#include "decoder.h"

static int tfa_pool_thermometer_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int checksum, checksum_rx, device, channel, battery;
    int temp_raw;
    float temp_f;

    // require 7 of 10 repeats
    int row = bitbuffer_find_repeated_row(bitbuffer, 7, 28);
    if (row < 0) {
        return DECODE_ABORT_EARLY; // no repeated row found
    }
    if (bitbuffer->bits_per_row[row] != 28) {
        return DECODE_ABORT_LENGTH; // prevent false positives
    }

    b = bitbuffer->bb[row];

    checksum_rx = ((b[0] & 0xF0) >> 4);
    device      = ((b[0] & 0x0F) << 4) + ((b[1] & 0xF0) >> 4);
    temp_raw    = ((b[1] & 0x0F) << 8) + b[2];
    temp_f      = (temp_raw > 2048 ? temp_raw - 4096 : temp_raw) * 0.1;
    channel     = ((b[3] & 0xC0) >> 6);
    battery     = ((b[3] & 0x20) >> 5);

    checksum = ((b[0] & 0x0F) +
                (b[1] >> 4) +
                (b[1] & 0x0F) +
                (b[2] >> 4) +
                (b[2] & 0x0F) +
                (b[3] >> 4) - 1);

    if (checksum_rx != (checksum & 0x0F)) {
        if (decoder->verbose > 1)
            bitrow_printf(b, bitbuffer->bits_per_row[row], "%s: checksum fail (%02x) ", __func__, checksum);
        return DECODE_FAIL_MIC;
    }

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING,    _X("TFA-Pool","TFA pool temperature sensor"),
            "id",               "Id",               DATA_INT,       device,
            "channel",          "Channel",          DATA_INT,       channel,
            "battery_ok",       "Battery",          DATA_INT,       battery,
            "temperature_C",    "Temperature",      DATA_FORMAT,    "%.01f C",  DATA_DOUBLE,    temp_f,
            "mic",              "Integrity",        DATA_STRING,    "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;

}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "mic",
        NULL,
};

r_device tfa_pool_thermometer = {
        .name        = "TFA pool temperature sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4600,
        .gap_limit   = 7800,
        .reset_limit = 10000,
        .decode_fn   = &tfa_pool_thermometer_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
