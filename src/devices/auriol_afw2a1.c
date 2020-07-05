/** @file
    Auriol AFW 2 A1 sensor.

    Copyright (C) 2019 LiberationFrequency

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Lidl Auriol AFW 2 A1 sensor (IAN 311588).

Technical data for the external sensor:
- Temperature measuring range/accuracy:       -20 to +65°C (-4 to +149°F) / ±1.5 °C (± 2.7 °F)
- Relative humidity measuring range/accuracy: 20 to 99% / ± 5%
- Relative humidity resolution:               1%
- Transmission frequencies:                   433 MHz (ch1:~433919300,ch2:~433915200,ch3:~433918000, various?)
- Transmission output:                        < 10 dBm / < 10 mW

The ID is retained even if the batteries are changed.
The device has three channels and a transmit button.

Data layout:
The sensor transmits 12 identical messages of 36 bits in a single package each ~60 seconds, depending on the temperature.
e.g.:

    [00] {36} 90 80 ba a3 a0 : 10010000 10000000 10111010 10100011 1010
    ...
    [11] {36} 90 80 ba a3 a0 : ...
     0           1           2           3           4
     9    0      8    0      b    a      a    3      a    0
    |1001|0000| |1000|0000| |1011|1010| |1010|0011| |1010|
    |id       | |chan|temp            | |fix |hum        |
    --------------------------------------------------------
    10010000  = id=0x90=144; 8 bit
    1         = battery_ok; 1 bit
    0         = tx_button; 1 bit
    00        = channel; 2 bit
    0000      = temperature leading sign,
                1110=0xe(-51.1°C to -25.7°C),
                1111=0xf(-25.6°C to - 0.1°C),
                0000=0x0(  0.0°C to  25,5°C),
                0001=0x1( 25.6°C to  51.1°C),
                0010=0x2( 51.2°C to  76.7°C); 4 bit
    10111010  = temperature=0xba=186=18,6°C; 8 bit
    1010      = fixed; 4 bit
    0011 1010 = humidity=0x3a=58%; 8 bit
*/

#include "decoder.h"

static int auriol_afw2a1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int row;
    int id;
    int channel;
    int battery_ok;
    int tx_button;
    int16_t temp_raw;
    float temp_c;
    int humidity;

    row = bitbuffer_find_repeated_row(bitbuffer, 12, 36);
    if (row < 0) {
        return DECODE_ABORT_EARLY; // no repeated row found
    }

    b = bitbuffer->bb[row];

    id          = b[0];
    battery_ok  = b[1] >> 7;
    tx_button   = (b[1] & 0x40) >> 6;
    channel     = (b[1] & 0x30) >> 4;
    temp_raw    = ((b[1] & 0x0f) << 12) | (b[2] << 4); // use sign extend
    temp_c      = (temp_raw >> 4) * 0.1f;
    // 0xa is fixed. If it differs, it is a wrong device. Could anyone confirm that?
    if ((b[3] >> 4) != 0xa) {
        if (decoder->verbose) {
            fprintf(stderr, "Not an Auriol-AFW2A1 device\n");
        }
        return DECODE_FAIL_SANITY;
    }
    humidity = (((b[3] & 0x0f) << 4) | (b[4] >> 4));

    if ((humidity > 0x64) || (humidity < 0x00) || (temp_c < -51.1) || (temp_c > 76.7)) {
        if (decoder->verbose) {
            fprintf(stderr, "Auriol-AFW2A1 data error\n");
        }
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data = data_make(
            "model",            "",                  DATA_STRING, "Auriol-AFW2A1",
            "id",               "",                  DATA_INT,    id,
            "channel",          "Channel",           DATA_INT,    channel + 1,
            "battery_ok",       "Battery",           DATA_INT,    battery_ok,
            "button",           "Button",            DATA_INT,    tx_button,
            "temperature_C",    "Temperature",       DATA_FORMAT, "%.1f C",  DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",          DATA_FORMAT, "%.0f %%", DATA_DOUBLE, (float)humidity,
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
        "button",
        "temperature_C",
        "humidity",
        NULL,
};

// ToDo: The timings have come about through trial and error. Audit this against weak signals!
r_device auriol_afw2a1 = {
        .name        = "Auriol AFW2A1 temperature/humidity sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 576,
        .long_width  = 1536,
        .sync_width  = 0, // No sync bit used
        .gap_limit   = 2012,
        .reset_limit = 3954,
        .decode_fn   = &auriol_afw2a1_decode,
        .disabled    = 0, // No side effects known.
        .fields      = output_fields,
};
