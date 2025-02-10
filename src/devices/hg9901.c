/** @file
    Moisture Sensor HG9901 - Homelead, Reyke, Dr.meter, Vodeson, Midlocater, Kithouse, Vingnut.

    Copyright (C) 2025 Inonoob

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Moisture Sensor HG9901 - Moisture Sensor HG9901 - Homelead, Reyke, Dr.meter, Vodeson, Midlocater, Kithouse, Vingnut.

This device is a simple garden temperature/moisture transmitter with a small LCD display for local viewing.

A message seems to have 65 bits.

Data layout:

    Byte 0   Byte 1   Byte 2   Byte 3   Byte 4   Byte 5   Byte 6   Byte 7   Byte 8
     55	      aa       30       06       e4        ff      0f        3f       8

Format string:

  SENSOR_ID:hhhh Soil moisture:hh Temperature:hh Battery:h Light intensity:hh unknown:b

Example packets:

  55aa 3006 e4 ff 0f 3f 8
  55aa 7f29 de fc 0f 1f 8

inverted:

 aa55 cff9 1b 00 f 0c 07
 aa55 80d6 21 03 f 0e 07

The sensor will send a message every 31 min if no changes are measured.
If changes are measured the sensor will instantly send messages.

Light intensity mapping (just a guess so far):
- LOW-: 0 - 10
- LOW: 11 - 30
- LOW+: 31 - 33
- NOR-: 34 - 50
- NOR: 51 - 66
- NOR+: 67 - 70
- HIGH-: 71 - 77
- HIGH: 78 - 85
- HIGH+: 86 - 133

*/
static int hg9901_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Loop through all rows in the bitbuffer to process all potential packets
    for (int i = 0; i < bitbuffer->num_rows; ++i) {
        uint8_t *b = bitbuffer->bb[i];

        // Ensure that the length of the packet is correct (65 bits)
        if (bitbuffer->bits_per_row[i] != 65) {
            continue;  // Skip this row if the length is not correct
        }

        // Check if the first two bytes are the correct preamble (0x55 0xAA)
        if (b[0] != 0x55 || b[1] != 0xaa) {
            continue;  // Skip this row if the preamble is invalid
        }

        decoder_log_bitbuffer(decoder, 2, __func__, bitbuffer, "After preamble check");

        // Invert the bits of the entire packet (only if valid)
        bitbuffer_invert(bitbuffer);

        decoder_log_bitbuffer(decoder, 2, __func__, bitbuffer, "After invert");

        // Extract the data from the packet (after inversion)
        int sensor_id       = (b[2] << 8) | (b[3]);               // Sensor ID from bytes 2 and 3
        int soil_moisture   = b[4];                               // Humidity from byte 4
        int temp_raw        = b[5];                               // Temperature from byte 5
        int battery_raw     = (b[6] & 0xf0) >> 4;                 // Battery status is together with the light value
        int light_intensity = ((b[6] & 0x0f) << 4) | (b[7] >> 4); // Light Intensity needs to be put togther from byte 6 and byte 7
        int light_lux       = light_intensity * 100;

        if (temp_raw >= 0x80) { // temperature has a dedicated sign bit
            temp_raw = 0x80 - temp_raw;
        }

        float battery_pct = battery_raw * 0.066667f; // Observed battery states are 0x3, 0x7, 0xb, 0xf (i.e. 0-15)

        /* clang-format off */
        data_t *data = data_make(
                "model",            "",                 DATA_STRING, "HG9901",
                "id",               "Sensor ID",        DATA_INT,    sensor_id,
                "battery_ok",       "Battery",          DATA_DOUBLE, battery_pct,
                "temperature_C",    "Temperature",      DATA_FORMAT, "%d C", DATA_INT, temp_raw,
                "moisture",         "Soil moisture",    DATA_FORMAT, "%u %%", DATA_INT, soil_moisture,
                "light_lux",        "Light",            DATA_FORMAT, "%u lux", DATA_INT, light_lux,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;  // Successfully decoded one valid packet and returned
    }

    return DECODE_ABORT_LENGTH;  // No valid packet found
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "moisture",
        "light_lux",
        NULL,
};

r_device const hg9901 = {
        .name        = "HG9901 moisture sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 600,  // plus gap 1000
        .long_width  = 1400, // plus gap 200
        .gap_limit   = 1200,
        .reset_limit = 4000, // packet gap is 3800
        .decode_fn   = &hg9901_decode,
        .disabled    = 1, // Disabled by default as there is no checksum
        .fields      = output_fields,
};
