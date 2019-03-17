/*
 * GT-WT-02 sensor on 433.92MHz
 *
 * Copyright (C) 2015 Paul Ortyl
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

/* Example and frame description provided by https://github.com/ludwich66

   [01] {37} 34 00 ed 47 60 : 00110100 00000000 11101101 01000111 01100000
   code, BatOK,not-man-send, Channel1, +23,7°C, 35%

   [01] {37} 34 8f 87 15 90 : 00110100 10001111 10000111 00010101 10010000
   code, BatOK,not-man-send, Channel1,-12,1°C, 10%

   Humidity:
    * the working range is 20-90 %
    * if "LL" in display view it sends 10 %
    * if "HH" in display view it sends 110%

   SENSOR: GT-WT-02 (ALDI Globaltronics..)
   TYP AAAAAAAA BCDDEFFF FFFFFFFF GGGGGGGx xxxxx

   TYPE Description:
   A = Random Device Code, changes with battery reset
   B = Battery 0=OK 1=LOW
   C = Manual Send Button Pressed 0=not pressed 1=pressed
   D = Channel 00=CH1, 01=CH2, 10=CH3
   E = Temp 0=positive 1=negative
   F = Positive Temp = 12 Bit bin2dez Temp,
   F = Negative Temp = 4095+1- F (12Bit bin2dez) , Factor Divid F / 10 (1Dezimal)
   G = Humidity = 7 Bit bin2dez 00-99, Display LL=10%, Display HH=110% (Range 20-90%)
   x = Checksum

   bin2dez(Bit1;Bit2;Bit3;Bit4)+ #rolling code
   bin2dez(Bit5;Bit6;Bit7;Bit8)+ #rolling code
   bin2dez(Bit9;Bit10;Bit11;Bit12)+ # send, bat , ch
   bin2dez(Bit13;Bit14;Bit15;Bit16)+ #temp1
   bin2dez(Bit17;Bit18;Bit19;Bit20)+ #temp2
   bin2dez(Bit21;Bit22;Bit23;Bit24)+ #temp3
   bin2dez(Bit25;Bit26;Bit27;Bit28)+ #hum1
   bin2dez(Bit29;Bit30;Bit31;Bit=0) = #hum2
   bin2dez(Bit32;Bit33;Bit34;Bit35;Bit36;Bit37) #checksum
   checksum = sum modulo 64
 */

#include "decoder.h"

static int gt_wt_02_process_row(r_device *decoder, bitbuffer_t *bitbuffer, int row)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[row];

    if (37 != bitbuffer->bits_per_row[row]
            || !(b[0] || b[1] || b[2] || b[3] || b[4])) /* exclude all zeros */
        return 0;

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
        return 0;

    // humidity: see above the note about working range
    int humidity = (b[3]>>1);  // extract bits for humidity
    if (humidity <= 10) // actually the sensors sends 10 below working range of 20%
        humidity = 0;
    else if (humidity > 90) // actually the sensors sends 110 above working range of 90%
        humidity = 100;

    int sensor_id      =  b[0];                    /* 8 x A */
    int battery_low    = (b[1] >> 7 & 1);          /* 1 x B */
    int button_pressed = (b[1] >> 6 & 1);          /* 1 x C */
    int channel        = (b[1] >> 4 & 3);          /* 2 x D */
    int negative_sign  = (b[1] >> 3 & 1);          /* 1 x E */
    int temp           = (((b[1] & 15) << 8) | b[2]); /* E + 11 X G */
    float temp_c       = (negative_sign ? (temp - (1 << 12)) : temp) * 0.1F;

    data = data_make(
            "model",            "",             DATA_STRING, "GT-WT02",
            "id",               "ID Code",      DATA_INT,    sensor_id,
            "channel",          "Channel",      DATA_INT,    channel + 1,
            "battery",          "Battery",      DATA_STRING, battery_low ? "LOW" : "OK",
            "button",           "Button ",      DATA_INT,    button_pressed,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%.0f %%", DATA_DOUBLE, (double)humidity,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    decoder_output_data(decoder, data);
    return 1;
}

static int gt_wt_02_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int counter = 0;
    // iterate through all rows, return on first successful
    for(int row=0; row<bitbuffer->num_rows && !counter; row++)
        counter += gt_wt_02_process_row(decoder, bitbuffer, row);
    return counter;
}

static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "battery",
    "button",
    "temperature_C",
    "humidity",
    "mic",
    NULL
};

r_device gt_wt_02 = {
    .name          = "GT-WT-02 Sensor",
    .modulation    = OOK_PULSE_PPM,
    .short_width   = 2500, // 3ms (old) / 2ms (new)
    .long_width    = 5000, // 6ms (old) / 4ms (new)
    .gap_limit     = 8000, // 10ms (old) / 9ms (new) sync gap
    .reset_limit   = 12000,
    .decode_fn     = &gt_wt_02_callback,
    .disabled      = 0,
    .fields        = output_fields,
};
