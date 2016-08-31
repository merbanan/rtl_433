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

 /* Example and frame description provided by https://github.com/ludwich66

 [01] {37} 34 00 ed 47 60 : 00110100 00000000 11101101 01000111 01100000
 code, BatOK,not-man-send, Channel1, +23,7°C, 35%

 [01] {37} 34 8f 87 15 90 : 00110100 10001111 10000111 00010101 10010000
 code, BatOK,not-man-send, Channel1,-12,1°C, 10%

 Humidity:
  * the working range is 20-90 %
  * if „LL“ in display view it sends 10 %
  * if „HH“ in display view it sends 110%

 SENSOR: GT-WT-02 (ALDI Globaltronics..)
 TYP AAAAAAAA BCDDEFFF FFFFFFFF GGGGGGGx xxxxx
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
 x = checksum

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


static int gt_wt_02_process_row(int row, const bitbuffer_t *bitbuffer)
{
  const uint8_t *b = bitbuffer->bb[row];
  const int length = bitbuffer->bits_per_row[row];

  if ( 37 != length
      || !(b[0] || b[1] || b[2] || b[3] || b[4])) /* exclude all zeros */
    return 0;

  //fprintf(stderr, "GT-WT-02: %02x %02x %02x %02x %02x\n", b[0], b[1], b[2], b[3], b[4]);

  // sum 8 nibbles (use 31 bits, the last one fill with 0 on 32nd bit)
  const int sum_nibbles =
        (b[0] >> 4) + (b[0] & 0xF)
      + (b[1] >> 4) + (b[1] & 0xF)
      + (b[2] >> 4) + (b[2] & 0xF)
      + (b[3] >> 4) + (b[3] & 0xe);

  // put last 6 bits into a number
  const int checksum = ((b[3] & 1 )<<5) + (b[4]>>3);

  // accept only correct checksums, (sum of nibbles modulo 64)
  if ((sum_nibbles & 0x3F) != checksum)
    return 0;

  // humidity: see above the note about working range
  const int humidity = (b[3]>>1);  // extract bits for humidity
  char const * humidity_str;       // pointer passed to the final printf
  char humidity_str_buf[4]={0};    // buffer for humidity als decimal string
  if (10 == humidity)
    humidity_str = "LL";           // below working range of 20%
  else if (110 == humidity)
    humidity_str = "HH";           // above working range of 90%
  else if (20<= humidity && humidity <= 90)
  {
    snprintf(humidity_str_buf, 4, "%2d", humidity);
    humidity_str = humidity_str_buf;
  }
  else
    return 0;  // very unlikely, but the humidity is outside of valid range


  const int sensor_id      =  b[0];                    /* 8 x A */
  const int battery_low    = (b[1] >> 7 & 1);          /* 1 x B */
  const int button_pressed = (b[1] >> 6 & 1);          /* 1 x C */
  const int channel        = (b[1] >> 4 & 3);          /* 2 x D */
  const int negative_sign  = (b[1] >> 3 & 1);          /* 1 x E */
  const int temp           = (((b[1] & 15) << 8) | b[2]); /* E + 11 X G */


  float tempC = (negative_sign ? ( temp - (1<<12) ) : temp ) * 0.1F;
  {
    /* @todo: remove timestamp printing as soon as the controller takes this task */
    char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);

    /* @todo make temperature unit configurable, not printing both */
    fprintf(stdout, "%s GT-WT-02 Sensor %02x: battery %s, channel %d, button %d, temperature %3.1f C / %3.1f F, humidity %s%%\n"
        , time_str, sensor_id, battery_low ? "low" : "OK", channel+1, button_pressed
        , tempC, celsius2fahrenheit(tempC), humidity_str
        );
  }
  return 1;
}

static int gt_wt_02_callback(bitbuffer_t *bitbuffer)
{
  int counter = 0;
  // iterate through all rows, return on first successful
  for(int row=0; row<bitbuffer->num_rows && !counter; row++)
    counter += gt_wt_02_process_row(row, bitbuffer);
  return counter;
}

r_device gt_wt_02 = {
  .name          = "GT-WT-02 Sensor",
  .modulation    = OOK_PULSE_PPM_RAW,
  .short_limit   = 3000,
  .long_limit    = 6000,
  .reset_limit   = 10000,
  .json_callback = &gt_wt_02_callback,
  .disabled      = 1,
  .demod_arg     = 0,
};

// Test code
// gcc -I src/ -I include/ -std=gnu99 -D _TEST_DECODER src/devices/gt_wt_02.c src/util.c
#ifdef _TEST_DECODER
int main()
{
  bitbuffer_t bb;
  bb.num_rows = 1;
  bb.bits_per_row[0] = 37;
  const uint8_t b[4][5] =
    {
      {0x00, 0x00, 0x00, 0x00, 0x00}, // this one is excluded despite the correct checksum
      {0x34, 0x00, 0xed, 0x47, 0x60},
      {0x34, 0x8f, 0x87, 0x15, 0x90},
      {0x34, 0x00, 0xde, 0x77, 0x78},
    };

  for(int i=0; i<4; i++)
  {
    memcpy(bb.bb[0], b[i], 5);
    gt_wt_02_callback(&bb);
  }

/*
 * Result:
2015-08-16 19:08:16 GT-WT-02 Sensor 34: battery OK, channel 0, button 0, temperature 23.7 C / 74.7 F, humidity 35%
2015-08-16 19:08:16 GT-WT-02 Sensor 34: battery low, channel 0, button 0, temperature -12.1 C / 10.2 F, humidity LL%
2015-08-16 19:08:16 GT-WT-02 Sensor 34: battery OK, channel 0, button 0, temperature 22.2 C / 72.0 F, humidity 59%
*/

}

#endif
