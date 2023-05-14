/** @file
    Temperature/Humidity outdoor sensor TFA 30.3221.02.

    Copyright (C) 2020 Odessa Claude

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Temperature/Humidity outdoor sensor TFA 30.3221.02.

This is the same as LaCrosse-TX141THBv2 and should be merged.

S.a. https://github.com/RFD-FHEM/RFFHEM/blob/master/FHEM/14_SD_WS.pm

    0    4    | 8    12   | 16   20   | 24   28   | 32   36
    --------- | --------- | --------- | --------- | ---------
    0000 1001 | 0001 0110 | 0001 0000 | 0000 0111 | 0100 1001
    IIII IIII | BSCC TTTT | TTTT TTTT | HHHH HHHH | XXXX XXXX

- I:  8 bit random id (changes on power-loss)
- B:  1 bit battery indicator (0=>OK, 1=>LOW)
- S:  1 bit sendmode (0=>auto, 1=>manual)
- C:  2 bit channel valid channels are 0-2 (1-3)
- T: 12 bit unsigned temperature, offset 500, scaled by 10
- H:  8 bit relative humidity percentage
- X:  8 bit checksum digest 0x31, 0xf4

The sensor sends 3 repetitions at intervals of about 60 seconds.
*/

#include "decoder.h"

static int tfa_303221_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row, sendmode, channel, battery_low, temp_raw, humidity;
    float temp_c;
    data_t *data;
    uint8_t *b;
    unsigned int device;

    // Device send 4 row, checking for two repeated
    row = bitbuffer_find_repeated_row(bitbuffer, (bitbuffer->num_rows > 4) ? 4 : 2, 40);
    if (row < 0)
        return DECODE_ABORT_EARLY;

    // Checking for right number of bits per row
    if (bitbuffer->bits_per_row[row] > 41)
        return DECODE_ABORT_LENGTH;

    bitbuffer_invert(bitbuffer);
    b = bitbuffer->bb[row];

    device = b[0];

    // Sanity Check
    if (device == 0)
        return DECODE_FAIL_SANITY;

    // Validate checksum
    int observed_checksum = b[4];
    int computed_checksum = lfsr_digest8_reflect(b, 4, 0x31, 0xf4);
    if (observed_checksum != computed_checksum) {
        return DECODE_FAIL_MIC;
    }

    temp_raw    = ((b[1] & 0x0F) << 8) | b[2];
    temp_c      = (temp_raw - 500) * 0.1f;
    humidity    = b[3];
    battery_low = b[1] >> 7;
    channel     = ((b[1] >> 4) & 3) + 1;
    sendmode    = (b[1] >> 6) & 1;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "TFA-303221",
            "id",               "Sensor ID",    DATA_INT,    device,
            "channel",          "Channel",      DATA_INT,    channel,
            "battery_ok",       "Battery",      DATA_INT,    !battery_low,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "sendmode",         "Test mode",    DATA_INT,    sendmode,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "sendmode",
        "mic",
        NULL,
};

r_device const tfa_30_3221 = {
        .name        = "TFA Dostmann 30.3221.02 T/H Outdoor Sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 235,
        .long_width  = 480,
        .reset_limit = 850,
        .sync_width  = 836,
        .decode_fn   = &tfa_303221_callback,
        .priority    = 10, // This is the same as LaCrosse-TX141THBv2
        .fields      = output_fields,
};
