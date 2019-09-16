/** @file
    Globaltronics GT-WT-03 sensor on 433.92MHz.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
*/
/**
Globaltronics GT-WT-03 sensor on 433.92MHz.

The 01-set sensor has 60 ms packet gap with 10 repeats.
The 02-set sensor has no packet gap with 23 repeats.

Example:

    {41} 17 cf be fa 6a 80 [ S1 C1 26,1 C 78.9 F 48% Bat-Good Manual-Yes ]
    {41} 17 cf be fa 6a 80 [ S1 C1 26,1 C 78.9 F 48% Bat-Good Manual-Yes Batt-Changed ]
    {41} 17 cf fe fa ea 80 [ S1 C1 26,1 C 78.9 F 48% Bat-Good Manual-No  Batt-Changed ]
    {41} 01 cf 6f 11 b2 80 [ S2 C2 23,8 C 74.8 F 48% Bat-LOW  Manual-No ]
    {41} 01 c8 d0 2b 76 80 [ S2 C3 -4,4 C 24.1 F 55% Bat-Good Manual-No  Batt-Changed ]

Format string:

    ID:8h HUM:8d B:b M:b C:2d TEMP:12d CHK:8h 1x

Data layout:

   TYP IIIIIIII HHHHHHHH BMCCTTTT TTTTTTTT XXXXXXXX

- I: Random Device Code: changes with battery reset
- H: Humidity: 8 Bit 00-99, Display LL=10%, Display HH=110% (Range 20-90%)
- B: Battery: 0=OK 1=LOW
- M: Manual Send Button Pressed: 0=not pressed, 1=pressed
- C: Channel: 00=CH1, 01=CH2, 10=CH3
- T: Temperature: 12 Bit 2's complement, scaled by 10
- X: Checksum, unknown

Humidity:
- the working range is 20-90 %
- if "LL" in display view it sends 10 %
- if "HH" in display view it sends 110%

*/

#include "decoder.h"

static int gt_wt_03_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int row = 0;
    uint8_t *b = bitbuffer->bb[row];

    // nominal 1 row or 23 rows, require more than half to match
    if (bitbuffer->num_rows > 1)
        row = bitbuffer_find_repeated_row(bitbuffer, bitbuffer->num_rows / 2 + 1, 41);

    if (row < 0)
        return DECODE_ABORT_LENGTH;

    if (41 != bitbuffer->bits_per_row[row])
        return DECODE_ABORT_LENGTH;

    bitbuffer_invert(bitbuffer);
    b = bitbuffer->bb[row];

    if (!(b[0] || b[1] || b[2] || b[3] || b[4])) /* exclude all zeros */
        return DECODE_ABORT_EARLY;

    // TODO: accept only correct checksum
    //if (... != checksum)
    //    return DECODE_FAIL_MIC;

    // humidity: see above the note about working range
    int humidity = b[1]; // extract 8 bits humidity
    if (humidity <= 10) // actually the sensors sends 10 below working range of 20%
        humidity = 0;
    else if (humidity > 90) // actually the sensors sends 110 above working range of 90%
        humidity = 100;

    int sensor_id      = (b[0]);          // 8 bits
    int battery_low    = (b[2] >> 7 & 1); // 1 bits
    int button_pressed = (b[2] >> 6 & 1); // 1 bits
    int channel        = (b[2] >> 4 & 3); // 2 bits
    int temp_raw       = (int16_t)(((b[2] & 0x0f) << 12) | b[3] << 4) >> 4; // uses sign extend
    float temp_c       = temp_raw * 0.1F;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "GT-WT03",
            "id",               "ID Code",      DATA_INT,    sensor_id,
            "channel",          "Channel",      DATA_INT,    channel + 1,
            "battery_ok",       "Battery",      DATA_INT,    !battery_low,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%.0f %%", DATA_DOUBLE, (double)humidity,
            "button",           "Button",       DATA_INT,    button_pressed,
            //"mic",              "Integrity",    DATA_STRING, "TODO",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "battery",
        "temperature_C",
        "humidity",
        "button",
        "mic",
        NULL,
};

r_device gt_wt_03 = {
        .name        = "Globaltronics GT-WT-03 Sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 256,
        .long_width  = 625,
        .sync_width  = 855,
        .gap_limit   = 1000,
        .reset_limit = 61000,
        .decode_fn   = &gt_wt_03_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
