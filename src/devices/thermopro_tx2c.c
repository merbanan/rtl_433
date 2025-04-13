/** @file
    ThermoPro TX-2C Outdoor Thermometer and humidity sensor.

    Copyright (C) 2023 igor@pele.tech.
    Copyright (C) 2023 maxime@werlen.fr.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ThermoPro TX-2C Outdoor Thermometer.

Example data:

    [00] { 7} 00
    [01] {45} 95 00 ff e0 a0 00
    [02] {45} 95 00 ff e0 a0 00
    [03] {45} 95 00 ff e0 a0 00
    [04] {45} 95 00 ff e0 a0 00
    [05] {45} 95 00 ff e0 a0 00
    [06] {45} 95 00 ff e0 a0 00
    [07] {45} 95 00 ff e0 a0 00
    [08] {36} 95 00 ff e0 a0

Data layout:

    [type] [id0] [id1] [flags] [temp0] [temp1] [temp2] [humi0] [humi1] [zero] [zero] [zero]

- type: 4 bit fixed 1001 (9) or 0110 (5)
- id: 8 bit a random id that is generated when the sensor starts, could include battery status
  the same batteries often generate the same id
- flags(3): is 1 when the battery is low, otherwise 0 (ok)
- flags(2): is 1 when the sensor sends a reading when pressing the button on the sensor
- flags(1,0): the channel number that can be set by the sensor (1, 2, 3, X)
- temp: 12 bit signed scaled by 10
- humi: 8 bit always 00001010 (0x0A) if no humidity sensor is available
- zero : a trailing 12 bit fixed 000000000000

*/

static int thermopro_tx2c_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Compare first four bytes of rows that have 45 or 36 bits.
    int row = bitbuffer_find_repeated_row(bitbuffer, 4, 36);
    if (row < 0)
        return DECODE_ABORT_EARLY;
    uint8_t *b = bitbuffer->bb[row];

    if (bitbuffer->bits_per_row[row] > 45)
        return DECODE_ABORT_LENGTH;

    // No need to decode/extract values for simple test
    if ((!b[0] && !b[1] && !b[2] && !b[3])
            || (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff && b[3] == 0xff)) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00 or 0xFF");
        return DECODE_FAIL_SANITY;
    }

    // check existing 12 bit 0 trailer
    if ((b[4] & 0x0F) != 0x00 || b[5] != 0x00)
        return DECODE_FAIL_SANITY;

    // int type     = b[0] >> 4;
    int id       = (((b[0] & 0xF) << 4) | (b[1] >> 4));
    int battery  = (b[1] & 0x08) >> 3;
    int button   = (b[1] & 0x04) >> 2;
    int channel  = (b[1] & 0x03) + 1;
    int temp_raw = (int16_t)((b[2] << 8) | b[3]); // uses sign-extend
    float temp_c = (temp_raw >> 4) * 0.1f;
    int humidity = (((b[3] & 0xF) << 4) | (b[4] >> 4));

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",            DATA_STRING, "Thermopro-TX2C",
            // "subtype",       "",            DATA_INT, type,
            "id",            "Id",          DATA_INT, id,
            "channel",       "Channel",     DATA_INT, channel,
            "battery_ok",    "Battery",     DATA_INT, !battery,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",      "Humidity",    DATA_COND, humidity != 0x0a, DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "button",        "Button",      DATA_INT, button,
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        // "subtype",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "button",
        NULL,
};

r_device const thermopro_tx2c = {
        .name        = "ThermoPro TX-2C Thermometer and Humidity sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1958,
        .long_width  = 3825,
        .gap_limit   = 3829,
        .reset_limit = 8643,
        .decode_fn   = &thermopro_tx2c_decode,
        .fields      = output_fields,
        .disabled    = 1, // default disabled because there is no checksum
};
