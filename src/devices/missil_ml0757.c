/** @file
    Missil ML0757 weather station with temperature, wind and rain sensor.

    Copyright (C) 2020 Marius Lindvall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Missil ML0757 weather station with temperature, wind and rain sensor.

The unit sends two different alternating packets, one for temperature and one
for rainfall and wind. All packets are 40 bits and are transferred 9 times.
Packet structure appears to be as follows:

                         BIT
          0   1   2   3   4   5   6   7   8
      0x0 +---+---+---+---+---+---+---+---+
          | Device ID                     |
      0x1 +---+---+---+---+---+---+---+---+
    B     |BAT| ? | ? | ? | ? |RWP| ? | ? | <-- FLAGS BYTE
    Y 0x2 +---+---+---+---+---+---+---+---+
    T     | Data field 1                 >|
    E 0x3 +---+---+---+---+---+---+---+---+
          |<Data field 1  | Data field 2 >|
      0x4 +---+---+---+---+---+---+---+---+
          |<Data field 2  | 1 | 1 | 1 | 1 |
      0x5 +---+---+---+---+---+---+---+---+

When flag bit RWP is not set, data field 1 is (temp in °C * 10) as a signed
12-bit integer, and data field 2 (8 bits) is unknown.

Where when bit RWP is set, data field 1 is accumulated rainfall in number of
steps as a signed 12-bit integer, where each step is 0.45 mm of rain, but when
the sign bit flips, the counter appears to reset to 0. Data field 2 is wind
speed as an 8 bit integer where 0x00 = 0 km/h, 0x80 = 1.4 km/h, 0xC0 = 2.8 km/h,
and any other value is ((value + 2) * 1.4) km/h. I know this doesn't intuitively
make sense, but it's what my testing has come up with and it matches what is
shown on the display.

The BAT flag is set if the transmitter has low battery. The other flags could
not be determined.

Packets are sent in sequences of type temp, rain+wind, temp, rain+wind, etc.
with ~36-37 seconds between each packet.

All packets begin with an empty row in addition to the 9 rows of repeated data.
*/

#include "decoder.h"

#define MISSIL_ML0757_FLAG_RWP  0x04 // Rain+Wind packet flag
#define MISSIL_ML0757_FLAG_BAT  0x80 // Battery low flag

static int missil_ml0757_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int id, flags, f12bit, f8bit;
    float temp_c, rainfall, wind_kph;
    int flag_bat, flag_rwp;

    int r = bitbuffer_find_repeated_row(bitbuffer, 5, 40);
    if (r < 0)
        return DECODE_ABORT_EARLY;

    b = bitbuffer->bb[r];

    if (bitbuffer->bits_per_row[0] > 0)
        return DECODE_ABORT_EARLY; // First row must be 0-length

    if (bitbuffer->bits_per_row[r] > 40)
        return DECODE_ABORT_LENGTH; // Message too long

    if ((b[4] & 0x0F) != 0x0F)
        return DECODE_ABORT_EARLY; // Tail bits not 1111

    // Read fields from sensor data
    id     = b[0];
    flags  = b[1];
    f12bit = (int16_t)((b[2] << 4) | (b[3] >> 4)) & 0xFFF;
    f8bit  = (((b[3] & 0x0F) << 4) | (b[4] >> 4)) & 0xFF;

    // Parse flags
    flag_bat = flags & MISSIL_ML0757_FLAG_BAT;
    flag_rwp = flags & MISSIL_ML0757_FLAG_RWP;

    // Parse temperature
    if (f12bit & 0x800) // Sign bit set; negative temperature
        temp_c = (0x1000 - f12bit) * -0.1f;
    else
        temp_c = f12bit * 0.1f;

    // Parse rainfall
    rainfall = f12bit * 0.45f;

    // Parse wind speed
    switch (f8bit) {
    case 0x00: wind_kph = 0.0f; break;
    case 0x80: wind_kph = 1.4f; break;
    case 0xC0: wind_kph = 2.8f; break;
    default: wind_kph = (f8bit + 2) * 1.4f; break;
    }

    if (flag_rwp) { // Rainwall and wind
        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Missil-ML0757",
                "id",               "ID",           DATA_INT,    id,
                "battery_ok",       "Battery",      DATA_INT,    !flag_bat,
                "rain_mm",          "Total rain",   DATA_FORMAT, "%.2f mm", DATA_DOUBLE, rainfall,
                "wind_avg_km_h",    "Wind speed",   DATA_FORMAT, "%.2f km/h", DATA_DOUBLE, wind_kph,
                NULL);
        /* clang-format on */
    }
    else { // Temperature
        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Missil-ML0757",
                "id",               "ID",           DATA_INT,    id,
                "battery_ok",       "Battery",      DATA_INT,    !flag_bat,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
                NULL);
        /* clang-format on */
    }

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "wind_avg_km_h",
        "rain_mm",
        NULL,
};

r_device const missil_ml0757 = {
        .name        = "Missil ML0757 weather station",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 975,
        .long_width  = 1950,
        .gap_limit   = 2500,
        .reset_limit = 4500,
        .tolerance   = 100,
        .decode_fn   = &missil_ml0757_callback,
        .fields      = output_fields,
};
