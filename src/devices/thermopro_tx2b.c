/** @file
    ThermoPro TX-2B Outdoor Thermometer and humidity sensor.

    Copyright (C) 2025 Philippe McLean <philippe.mclean@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ThermoPro TX-2B Outdoor Thermometer and Humidity sensor.

Commonly operates at 915 MHz (North America ISM band).

Example data:

    time      : 2025-12-02 16:30:49
    [00] { 7} 02
    [01] {45} 9c 30 04 a6 08 08
    [02] {45} 9c 30 04 a6 08 08
    [03] {45} 9c 30 04 a6 08 08
    [04] {45} 9c 30 04 a6 08 08
    [05] {45} 9c 30 04 a6 08 08
    [06] {45} 9c 30 04 a6 08 08
    [07] {45} 9c 30 04 a6 08 08
    [08] {36} 9c 30 04 a6 0

Data layout:

    [type] [id0] [id1] [flags] [temp0] [temp1] [temp2] [humi0] [humi1] [trailer] [trailer] [trailer]

- type: 4 bit fixed 1001 (9) or 0110 (5)
- id: 8 bit a random id that is generated when the sensor starts, could include battery status
  the same batteries often generate the same id
- flags(3): is 1 when the battery is low, otherwise 0 (ok)
- flags(2): is 1 when the sensor sends a reading when pressing the button on the sensor
- flags(1,0): the channel number that can be set by the sensor (1, 2, 3, X)
- temp: 12 bit signed scaled by 10 (no offset, unlike TX2/TX-2C which use offset 400)
- humi: 8 bit humidity percentage
- trailer: 12 bit trailing data (observed values: 0x808, may vary)

Example decode:
    Hex: 9c3004a60808
    Type: 0x9, ID: 0xC3, Channel: 1, Battery: OK, Button: Not pressed
    Temp: 0x04A (74) -> 7.4°C
    Humidity: 0x60 (96) -> 96%
    Trailer: 0x808

*/

static int thermopro_tx2b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
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

    // TX-2B has a trailer that is typically 0x808, but may vary
    // Unlike TX-2C which expects 0x000, we allow any trailer value
    // to avoid false negatives

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
            "model",         "",            DATA_STRING, "Thermopro-TX2B",
            // "subtype",       "",            DATA_INT, type,
            "id",            "Id",          DATA_INT, id,
            "channel",       "Channel",     DATA_INT, channel,
            "battery_ok",    "Battery",     DATA_INT, !battery,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
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

r_device const thermopro_tx2b = {
        .name        = "ThermoPro TX-2B Thermometer and Humidity sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1960,
        .long_width  = 2452,
        .gap_limit   = 7000,
        .reset_limit = 8588,
        .decode_fn   = &thermopro_tx2b_decode,
        .fields      = output_fields,
        .disabled    = 1, // default disabled because there is no checksum
};
