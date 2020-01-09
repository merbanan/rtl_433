/** @file
    Auriol HG02832 sensor.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Lidl Auriol HG02832 sensor, also Rubicson 48957 (Transmitter for 48956).

S.a. (#1161), (#1205).

Also works for the newer version HG05124A-DCF, IAN 321304_1901, version 07/2019.
However, the display occasionally shows 0.1 C incorrectly, especially with odd values.
But this is not an error of the evaluation of a single message, the sensor sends it this way.
Perhaps the value is averaged in the station.

PWM with 252 us short, 612 us long, and 860 us sync.
Preamble is a long pulsem, then 3 times sync pulse, sync gap, then data.
The 61ms packet gap is too long to capture repeats in one bitbuffer.

Data layout:

    II HH F TTT CC

- I: id, 8 bit
- H: humidity, 8 bit
- F: flags, 4 bit (Batt, TX-Button, Chan, Chan)
- T: temperature, 12 bit, deg. C scale 10
- C: checksum, 8 bit

*/

#include "decoder.h"

static int auriol_hg02832_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int id, humidity, batt_low, button, channel;
    int16_t temp_raw;
    float temp_c;

    if (bitbuffer->num_rows != 2)
        return DECODE_ABORT_EARLY;
    if (bitbuffer->bits_per_row[0] != 1 || bitbuffer->bits_per_row[1] != 40)
        return DECODE_ABORT_LENGTH;

    bitbuffer_invert(bitbuffer);

    b = bitbuffer->bb[1];

    // They tried to implement CRC-8 poly 0x31, but (accidentally?) reset the key every new byte.
    // (equivalent key stream is 7a 3d 86 43 b9 c4 62 31 repeated 4 times.)
    uint8_t d0 = b[0] ^ b[1] ^ b[2] ^ b[3];
    uint8_t chk = crc8(&d0, 1, 0x31, 0x53) ^ b[4];

    if (chk)
        return DECODE_FAIL_MIC; // prevent false positive checksum

    id       = b[0];
    humidity = b[1];
    //flags    = b[2] >> 4;
    batt_low = (b[2]) >> 7;
    button   = (b[2] & 0x40) >> 6;
    channel  = (b[2] & 0x30) >> 4;

    temp_raw = ((b[2] & 0x0f) << 12) | (b[3] << 4); // use sign extend
    temp_c = (temp_raw >> 4) * 0.1f;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Auriol-HG02832",
            "id",               "",             DATA_INT,    id,
            "channel",          "",             DATA_INT,    channel + 1,
            "battery_ok",       "Battery",      DATA_INT,    !batt_low,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%.0f %%", DATA_DOUBLE, (float)humidity,
            "button",           "Button",       DATA_INT,    button,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
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
        "humidity",
        "button",
        "mic",
        NULL,
};

r_device auriol_hg02832 = {
        .name        = "Auriol HG02832, HG05124A-DCF, Rubicson 48957 temperature/humidity sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 252,
        .long_width  = 612,
        .sync_width  = 860,
        .gap_limit   = 750,
        .reset_limit = 62990, // 61ms packet gap
        .decode_fn   = &auriol_hg02832_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
