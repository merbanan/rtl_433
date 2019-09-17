/** @file
    GT-WT-02 sensor on 433.92MHz.

    Copyright (C) 2015 Paul Ortyl

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3 as
    published by the Free Software Foundation.
*/
/**
GT-WT-02 sensor on 433.92MHz.

Example and frame description provided by https://github.com/ludwich66

   [01] {37} 34 00 ed 47 60 : 00110100 00000000 11101101 01000111 01100000
   code, BatOK,not-man-send, Channel1, +23,7°C, 35%

   [01] {37} 34 8f 87 15 90 : 00110100 10001111 10000111 00010101 10010000
   code, BatOK,not-man-send, Channel1,-12,1°C, 10%

Humidity:
- the working range is 20-90 %
- if "LL" in display view it sends 10 %
- if "HH" in display view it sends 110%

SENSOR: GT-WT-02 (ALDI Globaltronics..)

   TYP IIIIIIII BMCCTTTT TTTTTTTT HHHHHHHX XXXXX

TYPE Description:

- I = Random Device Code, changes with battery reset
- B = Battery 0=OK 1=LOW
- M = Manual Send Button Pressed 0=not pressed 1=pressed
- C = Channel 00=CH1, 01=CH2, 10=CH3
- T = Temperature, 12 Bit 2's complement, scaled by 10
- H = Humidity = 7 Bit bin2dez 00-99, Display LL=10%, Display HH=110% (Range 20-90%)
- X = Checksum, sum modulo 64

A Lidl AURIO (from 12/2018) with PCB marking YJ-T12 V02 has two extra bits in front.
*/

#include "decoder.h"

static int gt_wt_02_process_row(r_device *decoder, bitbuffer_t *bitbuffer, int row)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[row];
    uint8_t shifted[5];

    if (39 == bitbuffer->bits_per_row[row]) {
        bitbuffer_extract_bytes(bitbuffer, row, 2, shifted, 37);
        b = shifted;
    }
    else if (37 != bitbuffer->bits_per_row[row])
        return 0; // DECODE_ABORT_LENGTH

    if (!(b[0] || b[1] || b[2] || b[3] || b[4])) /* exclude all zeros */
        return 0; // DECODE_ABORT_EARLY

    // sum 8 nibbles (use 31 bits, the last one fill with 0 on 32nd bit)
    int sum_nibbles =
          (b[0] >> 4) + (b[0] & 0xF)
        + (b[1] >> 4) + (b[1] & 0xF)
        + (b[2] >> 4) + (b[2] & 0xF)
        + (b[3] >> 4) + (b[3] & 0xe);

    // put last 6 bits into a number
    int checksum = ((b[3] & 1) << 5) + (b[4] >> 3);

    // accept only correct checksums, (sum of nibbles modulo 64)
    if ((sum_nibbles & 0x3F) != checksum)
        return 0; // DECODE_FAIL_MIC

    // humidity: see above the note about working range
    int humidity = (b[3] >> 1); // extract bits for humidity
    if (humidity <= 10) // actually the sensors sends 10 below working range of 20%
        humidity = 0;
    else if (humidity > 90) // actually the sensors sends 110 above working range of 90%
        humidity = 100;

    int sensor_id      = (b[0]);          // 8 bits
    int battery_low    = (b[1] >> 7 & 1); // 1 bits
    int button_pressed = (b[1] >> 6 & 1); // 1 bits
    int channel        = (b[1] >> 4 & 3); // 2 bits
    int temp_raw       = (int16_t)(((b[1] & 0x0f) << 12) | b[2] << 4) >> 4; // uses sign extend
    float temp_c       = temp_raw * 0.1F;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "GT-WT02",
            "id",               "ID Code",      DATA_INT,    sensor_id,
            "channel",          "Channel",      DATA_INT,    channel + 1,
            "battery",          "Battery",      DATA_STRING, battery_low ? "LOW" : "OK",
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%.0f %%", DATA_DOUBLE, (double)humidity,
            "button",           "Button ",      DATA_INT,    button_pressed,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static int gt_wt_02_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int counter = 0;
    // iterate through all rows, return on first successful
    for (int row = 0; row < bitbuffer->num_rows && !counter; ++row)
        counter += gt_wt_02_process_row(decoder, bitbuffer, row);
    return counter;
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

r_device gt_wt_02 = {
        .name        = "Globaltronics GT-WT-02 Sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2500, // 3ms (old) / 2ms (new)
        .long_width  = 5000, // 6ms (old) / 4ms (new)
        .gap_limit   = 8000, // 10ms (old) / 9ms (new) sync gap
        .reset_limit = 12000,
        .decode_fn   = &gt_wt_02_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
