#include "rtl_433.h"
#include "util.h"

/*
 * GT-WT-02 sensor on 433.92MHz
 *
 * Copyright (C) 2015 Paul Ortyl
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

 /* NOTE: not everything is correct, more examples are necessary to
  * complete the protocol description
  *
  * Example provided by https://github.com/ludwich66

 2 examples
 [01] {37} 34 00 ed 47 60 : 00110100 00000000 11101101 01000111 01100000
 code, BatOK,not-man-send, Channel1, +23,7°C, 35%

 [01] {37} 34 8f 87 15 90 : 00110100 10001111 10000111 00010101 10010000
 code, BatOK,not-man-send, Channel1,-12,1°C, 10%

 SENSOR: GT-WT-02 (ALDI Globaltronics..)
 TYP AAAAAAAA BCDDEFFF FFFFFFFF FGGGGGGG xxxxx
 BIT 76543210 76543210 76543210 76543210 76543

 TYPDescriptian
 A = Rolling Device Code, Change after battery replacement
 B = Battery 0=OK 1=LOW
 C = Manual Send Button Pressed 0=not pressed 1=pressed
 D = Channel 00=CH1, 01=CH2, 11=CH3
 E = Temp 0=positive 1=negative
 F = PositiveTemp =12 Bit bin2dez Temp,
 F = negative Temp = 4095+1- F (12Bit bin2dez) , Factor Divid F / 10 (1Dezimal)
 G = Humidity = 7 Bit bin2dez 00-99
 x = unknown
*/


static int gt_wt_02_process_row(int row, const bitbuffer_t *bitbuffer);
static int gt_wt_02_callback(bitbuffer_t *bitbuffer)
{
  int counter = 0;
  for(int row=0; row<bitbuffer->num_rows; row++)
    counter += gt_wt_02_process_row(row, bitbuffer);
  return counter;
}

static int gt_wt_02_process_row(int row, const bitbuffer_t *bitbuffer)
{
  const uint8_t *b = bitbuffer->bb[row];
  const int length = bitbuffer->bits_per_row[row];

  if ( 37 != length)
    return 0;

  //fprintf(stderr, "GT-WT-02: %02x %02x %02x %02x %02x\n", b[0], b[1], b[2], b[3], b[4]);

  const int sensor_id      =  b[0];                    /* 8 x A */
  const int battery_low    = (b[1] >> 7 & 1);          /* 1 x B */
  const int button_pressed = (b[1] >> 6 & 1);          /* 1 x C */
  const int channel        = (b[1] >> 4 & 3);          /* 2 x D */
  const int negative_sign  = (b[1] >> 3 & 1);          /* 1 x E */
  const int temp           = (((b[1] & 15) << 8) | b[2]); /* E + 11 X G */
  const int humidity = (b[3]>>1) & 0x7F;

  float tempC = (negative_sign ? ( temp - (1<<12) ) : temp ) * 0.1F;
  {
    time_t time_now;
    char time_str[LOCAL_TIME_BUFLEN];
    time(&time_now);
    local_time_str(time_now, time_str);

    printf("%s GT-WT-02 Sensor %02x: battery %s, channel %d, button %d, temperature %3.1f C / %3.1f F, humidity %2d%%\n"
        , time_str, sensor_id, battery_low ? "low" : "OK", channel, button_pressed
        , tempC, celsius2fahrenheit(tempC), humidity
        );
  }
  return 1;
}

r_device gt_wt_02 = {
  .name          = "GT-WT-02 sensor",
  .modulation    = OOK_PULSE_PPM_RAW,
  .short_limit   = 1150,
  .long_limit    = 2500,
  .reset_limit   = 2500,
  .json_callback = &gt_wt_02_callback,
  .disabled      = 0,
  .demod_arg     = 0,
};

// Test code
// gcc -I src/ -I include/ -std=gnu99 -D _TEST_DECODER src/devices/gt_wt_02.c src/util.c
#ifdef _TEST_DECODER
int main()
{
  bitbuffer_t bb;
  bb.num_rows = 3;
  bb.bits_per_row[0] = 37;
  bb.bits_per_row[1] = 37;
  bb.bits_per_row[2] = 37;
  const uint8_t b0[] = {0x34, 0x00, 0xed, 0x47, 0x60};
  const uint8_t b1[] = {0x34, 0x8f, 0x87, 0x15, 0x90};
  const uint8_t b2[] = {0x34, 0x00, 0xde, 0x77, 0x78};

  memcpy(bb.bb[0], b0, 5);
  memcpy(bb.bb[1], b1, 5);
  memcpy(bb.bb[2], b2, 5);

  gt_wt_02_callback(&bb);
/*
 * Result:
2015-08-16 19:08:16 GT-WT-02 Sensor 34: battery OK, channel 0, button 0, temperature 23.7 C / 74.7 F, humidity 35%
2015-08-16 19:08:16 GT-WT-02 Sensor 34: battery low, channel 0, button 0, temperature -12.1 C / 10.2 F, humidity 10%
2015-08-16 19:08:16 GT-WT-02 Sensor 34: battery OK, channel 0, button 0, temperature 22.2 C / 72.0 F, humidity 59%
*/

}

#endif
