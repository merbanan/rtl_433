#include "rtl_433.h"
#include "util.h"
#include "data.h"

/*
 * TFA-Twin-Plus-30.3049
 *
 * Copyright (C) 2015 Paul Ortyl
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

/*
 * Protocol as reverse engineered by https://github.com/iotzo
 *
 * 36 Bits (9 nibbles)
 *
 * Type: IIIICCII B???TTTT TTTTTSSS HHHHHHH1 XXXX
 * BIT/8 76543210 76543210 76543210 76543210 7654
 * BIT/A 01234567 89012345 57890123 45678901 2345
 *       0          1          2          3
 * I: sensor ID (changes on battery change)
 * C: Channel number
 * B: low battery
 * T: temperature
 * S: sign
 * X: checksum
 * ?: unknown meaning
 * all values are LSB-first, so need to be reversed before presentation
 *
 *
 * [04] {36} e4 4b 70 73 00 : 111001000100 101101110 000 0111001 10000 ---> temp/hum:23.7/50
 * temp num-->13-21bit(9bits) in reverse order in this case "011101101"=237
 * positive temps ( with 000 in bits 22-24) : temp=num/10 (in this case 23.7 C)
 * negative temps (with 111 in bits 22-24) : temp=(512-num)/10
 * negative temps example:
 * [03] {36} e4 4c 1f 73 f0 : 111001000100 110000011 111 0111001 11111 temp: -12.4
 *
 * Humidity:
 * hum num-->25-32bit(7bits) in reverse order : in this case "1001110"=78
 * humidity=num-28 --> 78-28=50
 *
 * I have channel number bits(5,6 in reverse order) and low battery bit(9)
 * It seems that the 1,2,3,4,7,8 bits changes randomly on every reset/battery change.
 *
 */

inline static uint8_t reverse_byte(uint8_t byte)
{
  byte = (byte & 0xF0) >> 4 | (byte & 0x0F) << 4;
  byte = (byte & 0xCC) >> 2 | (byte & 0x33) << 2;
  byte = (byte & 0xAA) >> 1 | (byte & 0x55) << 1;
  return byte;
}

static int tfa_twin_plus_303049_process_row(int row, const bitbuffer_t *bitbuffer)
{
  data_t *data;
  const uint8_t *b      = bitbuffer->bb[row];
  const uint16_t length = bitbuffer->bits_per_row[row];

  if ((36 != length)
    || !(b[0] || b[1] || b[2] || b[3] || b[4])) /* exclude all zeros */
    return 0;

  //fprintf(stderr, "TFA-Twin-Plus-30.3049: %02x %02x %02x %02x %02x\n", b[0], b[1], b[2], b[3], b[4]);
  // reverse bit order
  const uint8_t rb[5] = { reverse_byte(b[0]), reverse_byte(b[1]), reverse_byte(b[2])
                        , reverse_byte(b[3]), reverse_byte(b[4]) };

  const int sum_nibbles =
        (rb[0] >> 4) + (rb[0] & 0xF)
      + (rb[1] >> 4) + (rb[1] & 0xF)
      + (rb[2] >> 4) + (rb[2] & 0xF)
      + (rb[3] >> 4) + (rb[3] & 0xF);

  const int checksum = rb[4] & 0x0F;  // just make sure the 10th nibble does not contain junk
  if (checksum != (sum_nibbles & 0xF))
    return 0; // wrong checksum

  /* IIIICCII B???TTTT TTTTTSSS HHHHHHH1 XXXX */
  const int negative_sign = (b[2] & 7);
  const int temp          = ((rb[2]&0x1F) << 4) | (rb[1]>> 4);
  const int humidity      = (rb[3] & 0x7F) - 28;
  const int sensor_id     = (rb[0] & 0x0F) | ((rb[0] & 0xC0)>>2);
  const int battery_low   = b[1] >> 7;
  const int channel       = (b[0]>>2) & 3;

  float tempC = (negative_sign ? -( (1<<9) - temp ) : temp ) * 0.1F;
  {
    /* @todo: remove timestamp printing as soon as the controller takes this task */
    char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);

    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, "TFA-Twin-Plus-30.3049",
                     "id",            "",            DATA_INT, sensor_id,
                     "channel",       "",            DATA_INT, channel,
                     "battery",       "Battery",     DATA_STRING, battery_low ? "LOW" : "OK",
                     "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, tempC,
                     "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                     NULL);
    data_acquired_handler(data);
  }

  return 1;
}

static int tfa_twin_plus_303049_callback(bitbuffer_t *bitbuffer)
{
  int counter = 0;
  for(int row=0; row<bitbuffer->num_rows; row++)
    counter += tfa_twin_plus_303049_process_row(row, bitbuffer);
  return counter;
}


// Test code
// gcc -I src/ -I include/ -std=gnu99 -ggdb -D _TEST_DECODER src/devices/tfa_twin_plus_30.3049.c src/util.c
#ifdef _TEST_DECODER
int main()
{
  bitbuffer_t bb;
  bb.num_rows = 1;
  bb.bits_per_row[0] = 36;
  const uint8_t b[75][5] =
    {
      { 0x00, 0x00, 0x00, 0x00, 0x00}, // this one is excluded despite the correct checksum
      { 0xe4, 0x4b, 0x70, 0x73, 0x00},  // temp/hum:23.7/50
      { 0xe4, 0x4c, 0x1f, 0x73, 0xf0},
      { 0xe4, 0x25, 0xc8, 0x87, 0x50},  // 111001000010 010111001 000 1000011 1 0101
      { 0xe4, 0x2e, 0xc8, 0xbb, 0x40},  // 111001000010 111011001 000 1011101 1 0100
      { 0xe4, 0x26, 0xc8, 0x9b, 0xb0},  // 111001000010 011011001 000 1001101 1 1011
      { 0xe4, 0x2a, 0xc8, 0x6b, 0x90},  // 111001000010 101011001 000 0110101 1 1001
      { 0xe4, 0x22, 0xc8, 0xcb, 0xa0},  // 111001000010 001011001 000 1100101 1 1010
      { 0xe4, 0x2c, 0xc8, 0x0b, 0x80},  // 111001000010 110011001 000 0000101 1 1000
      { 0xe4, 0x24, 0xc8, 0xb3, 0x30},  // 111001000010 010011001 000 1011001 1 0011
      { 0xe4, 0x28, 0xc8, 0xd3, 0x90},  // 111001000010 100011001 000 1101001 1 1001
      { 0xe4, 0x4f, 0x48, 0x63, 0xf0},  // 111001000100 111101001 000 0110001 1 1111
      { 0xe4, 0x47, 0x48, 0xa3, 0xb0},  // 111001000100 011101001 000 1010001 1 1011
      { 0xe4, 0x47, 0x48, 0x23, 0x30},  // 111001000100 011101001 000 0010001 1 0011
      { 0xe4, 0x40, 0x50, 0xa3, 0x60},  // 111001000100 000001010 000 1010001 1 0110
      { 0xe4, 0x44, 0xe0, 0x43, 0x40},  // 111001000100 010011100 000 0100001 1 0100
      { 0xe4, 0x4f, 0x20, 0xc3, 0xb0},  // 111001000100 111100100 000 1100001 1 1011
      { 0xe4, 0x42, 0xc0, 0x43, 0x00},  // 111001000100 001011000 000 0100001 1 0000
      { 0xe4, 0x4b, 0x80, 0xc3, 0x10},  // 111001000100 101110000 000 1100001 1 0001
      { 0xe4, 0x4d, 0x00, 0xc3, 0xa0},  // 111001000100 110100000 000 1100001 1 1010
      { 0xe4, 0x4d, 0xff, 0x23, 0x20},  // 111001000100 110111111 111 0010001 1 0010
      { 0xe4, 0x4b, 0x7f, 0x23, 0xa0},  // 111001000100 101101111 111 0010001 1 1010
      { 0xe4, 0x40, 0x7f, 0xa3, 0x90},  // 111001000100 000001111 111 1010001 1 1001
      { 0xe4, 0x4a, 0xbf, 0x93, 0x80},  // 111001000100 101010111 111 1001001 1 1000
      { 0xe4, 0x43, 0x3f, 0xe3, 0xa0},  // 111001000100 001100111 111 1110001 1 1010
      { 0xe4, 0x42, 0x3f, 0xe3, 0xb0},  // 111001000100 001000111 111 1110001 1 1011
      { 0xe4, 0x43, 0xdf, 0x13, 0xa0},  // 111001000100 001111011 111 0001001 1 1010
      { 0xe4, 0x4a, 0xdf, 0x93, 0xf0},  // 111001000100 101011011 111 1001001 1 1111
      { 0xe4, 0x47, 0x5f, 0x93, 0xe0},  // 111001000100 011101011 111 1001001 1 1110
      { 0xe4, 0x41, 0x5f, 0x53, 0x40},  // 111001000100 000101011 111 0101001 1 0100
      { 0xe4, 0x49, 0x9f, 0x33, 0x20},  // 111001000100 100110011 111 0011001 1 0010
      { 0xe4, 0x41, 0x1f, 0xb3, 0xc0},  // 111001000100 000100011 111 1011001 1 1100
      { 0xe4, 0x4c, 0x1f, 0x73, 0xf0},  // 111001000100 110000011 111 0111001 1 1111
      { 0x24, 0x01, 0xf0, 0x13, 0x80},  // 0010 0100 0000 0001 11110000 00010011 1000 CH1
      { 0x84, 0x02, 0x08, 0x83, 0xa0},  // 1000 0100 0000 0010 00001000 10000011 1010 CH1
      { 0x96, 0x02, 0x08, 0x83, 0x80},  // 1001 0110 0000 0010 00001000 10000011 1000 CH1
      { 0xc4, 0x0c, 0x08, 0x83, 0x60},  // 1100 0100 0000 1100 00001000 10000011 0110 CH1
      { 0x46, 0x0c, 0x08, 0x83, 0x90},  // 0100 0110 0000 1100 00001000 10000011 1001 CH1
      { 0x75, 0x0c, 0x08, 0x83, 0x90},  // 0111 0101 0000 1100 00001000 10000011 1001 CH1
      { 0x75, 0x04, 0x08, 0x03, 0xe0},  // 0111 0101 0000 0100 00001000 00000011 1110 CH1
      { 0x96, 0x8e, 0x88, 0xc3, 0x10},  // 1001 0110 1000 1110 10001000 11000011 0001 CH1/LOW BAT
      { 0x96, 0x81, 0x88, 0x83, 0xe0},  // 1001 0110 1000 0001 10001000 10000011 1110 CH1/LOW BAT
      { 0x96, 0x86, 0x88, 0x83, 0xa0},  // 1001 0110 1000 0110 10001000 10000011 1010 CH1/LOW BAT
      { 0xa8, 0x02, 0x48, 0xc3, 0x30},  // 1010 1000 0000 0010 01001000 11000011 0011 CH2
      { 0xa8, 0x40, 0x88, 0x83, 0xe0},  // 1010 1000 0100 0000 10001000 10000011 1110 CH2
      { 0x88, 0x05, 0x08, 0x83, 0x50},  // 1000 1000 0000 0101 00001000 10000011 0101 CH2
      { 0x0b, 0x09, 0x08, 0x43, 0xa0},  // 0000 1011 0000 1001 00001000 01000011 1010 CH2
      { 0x5b, 0x0a, 0x08, 0x83, 0x50},  // 0101 1011 0000 1010 00001000 10000011 0101 CH2
      { 0x7a, 0x0c, 0x08, 0x83, 0x20},  // 0111 1010 0000 1100 00001000 10000011 0010 CH2
      { 0xda, 0x04, 0x08, 0x83, 0x00},  // 1101 1010 0000 0100 00001000 10000011 0000 CH2
      { 0xb8, 0x08, 0x08, 0x83, 0xb0},  // 1011 1000 0000 1000 00001000 10000011 1011 CH2
      { 0xfb, 0x08, 0x08, 0x83, 0xd0},  // 1111 1011 0000 1000 00001000 10000011 1101 CH2
      { 0x68, 0x00, 0x08, 0x83, 0xa0},  // 0110 1000 0000 0000 00001000 10000011 1010 CH2
      { 0xea, 0x00, 0x08, 0x83, 0x50},  // 1110 1010 0000 0000 00001000 10000011 0101 CH2
      { 0x69, 0x00, 0x08, 0x83, 0xb0},  // 0110 1001 0000 0000 00001000 10000011 1011 CH2
      { 0xf8, 0x00, 0x08, 0x83, 0x70},  // 1111 1000 0000 0000 00001000 10000011 0111 CH2
      { 0xe9, 0x0f, 0xf0, 0x83, 0xd0},  // 1110 1001 0000 1111 11110000 10000011 1101 CH2
      { 0x1b, 0x0f, 0xf0, 0x83, 0x00},  // 0001 1011 0000 1111 11110000 10000011 0000 CH2
      { 0x98, 0x0f, 0xf0, 0x83, 0xa0},  // 1001 1000 0000 1111 11110000 10000011 1010 CH2
      { 0x4b, 0x07, 0xf0, 0x83, 0x90},  // 0100 1011 0000 0111 11110000 10000011 1001 CH2
      { 0xca, 0x07, 0xf0, 0x83, 0x40},  // 1100 1010 0000 0111 11110000 10000011 0100 CH2
      { 0x8b, 0x07, 0xf0, 0x83, 0x10},  // 1000 1011 0000 0111 11110000 10000011 0001 CH2
      { 0xda, 0x07, 0xf0, 0x83, 0x50},  // 1101 1010 0000 0111 11110000 10000011 0101 CH2
      { 0x29, 0x07, 0xf0, 0x83, 0xe0},  // 0010 1001 0000 0111 11110000 10000011 1110 CH2
      { 0x8b, 0x00, 0x08, 0x43, 0xb0},  // 1000 1011 0000 0000 00001000 01000011 1011 CH2
      { 0x2b, 0x0f, 0xf0, 0x83, 0x30},  // 0010 1011 0000 1111 11110000 10000011 0011 CH2
      { 0x59, 0x07, 0xf0, 0x83, 0xb0},  // 0101 1001 0000 0111 11110000 10000011 1011 CH2
      { 0xaa, 0x0a, 0x88, 0x63, 0xc0},  // 1010 1010 0000 1010 10001000 01100011 1100 CH2
      { 0x38, 0x08, 0x08, 0x43, 0xb0},  // 0011 1000 0000 1000 00001000 01000011 1011 CH2
      { 0xae, 0x08, 0x88, 0xc3, 0x70},  // 1010 1110 0000 1000 10001000 11000011 0111 CH3
      { 0x4f, 0x0d, 0x08, 0x83, 0x50},  // 0100 1111 0000 1101 00001000 10000011 0101 CH3
      { 0xbe, 0x05, 0x08, 0x83, 0x30},  // 1011 1110 0000 0101 00001000 10000011 0011 CH3
      { 0x3e, 0x01, 0x08, 0x83, 0x90},  // 0011 1110 0000 0001 00001000 10000011 1001 CH3
      { 0xbd, 0x01, 0x08, 0x83, 0x70},  // 1011 1101 0000 0001 00001000 10000011 0111 CH3
      { 0xac, 0x0e, 0x08, 0x83, 0xb0}   // 1010 1100 0000 1110 00001000 10000011 1011 CH3
    };

  int tests = 0;
  for(size_t i=0; i<sizeof(b)/sizeof(*b); i++)
  {
    memcpy(bb.bb[0], b[i], 5);
    tests += tfa_twin_plus_303049_callback(&bb);
  }
  fprintf(stderr, "Passed %d/%d positive tests\n", tests, 74); // there is one negative test
}

/*
TFA-Twin-Plus-30.3049: e4 4b 70 73 00
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 23.7 C / 74.7 F, humidity 50%
TFA-Twin-Plus-30.3049: e4 4c 1f 73 f0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -12.5 C / 9.5 F, humidity 50%
TFA-Twin-Plus-30.3049: e4 25 c8 87 50
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 31.4 C / 88.5 F, humidity 69%
TFA-Twin-Plus-30.3049: e4 2e c8 bb 40
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 31.1 C / 88.0 F, humidity 65%
TFA-Twin-Plus-30.3049: e4 26 c8 9b b0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 31.0 C / 87.8 F, humidity 61%
TFA-Twin-Plus-30.3049: e4 2a c8 6b 90
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 30.9 C / 87.6 F, humidity 58%
TFA-Twin-Plus-30.3049: e4 22 c8 cb a0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 30.8 C / 87.4 F, humidity 55%
TFA-Twin-Plus-30.3049: e4 2c c8 0b 80
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 30.7 C / 87.3 F, humidity 52%
TFA-Twin-Plus-30.3049: e4 24 c8 b3 30
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 30.6 C / 87.1 F, humidity 49%
TFA-Twin-Plus-30.3049: e4 28 c8 d3 90
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 30.5 C / 86.9 F, humidity 47%
TFA-Twin-Plus-30.3049: e4 4f 48 63 f0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 30.3 C / 86.5 F, humidity 42%
TFA-Twin-Plus-30.3049: e4 47 48 a3 b0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 30.2 C / 86.4 F, humidity 41%
TFA-Twin-Plus-30.3049: e4 47 48 23 30
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 30.2 C / 86.4 F, humidity 40%
TFA-Twin-Plus-30.3049: e4 40 50 a3 60
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 16.0 C / 60.8 F, humidity 41%
TFA-Twin-Plus-30.3049: e4 44 e0 43 40
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 11.4 C / 52.5 F, humidity 38%
TFA-Twin-Plus-30.3049: e4 4f 20 c3 b0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 7.9 C / 46.2 F, humidity 39%
TFA-Twin-Plus-30.3049: e4 42 c0 43 00
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 5.2 C / 41.4 F, humidity 38%
TFA-Twin-Plus-30.3049: e4 4b 80 c3 10
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 2.9 C / 37.2 F, humidity 39%
TFA-Twin-Plus-30.3049: e4 4d 00 c3 a0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature 1.1 C / 34.0 F, humidity 39%
TFA-Twin-Plus-30.3049: e4 4d ff 23 20
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -0.5 C / 31.1 F, humidity 40%
TFA-Twin-Plus-30.3049: e4 4b 7f 23 a0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -1.9 C / 28.6 F, humidity 40%
TFA-Twin-Plus-30.3049: e4 40 7f a3 90
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -3.2 C / 26.2 F, humidity 41%
TFA-Twin-Plus-30.3049: e4 4a bf 93 80
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -4.3 C / 24.3 F, humidity 45%
TFA-Twin-Plus-30.3049: e4 43 3f e3 a0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -5.2 C / 22.6 F, humidity 43%
TFA-Twin-Plus-30.3049: e4 42 3f e3 b0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -6.0 C / 21.2 F, humidity 43%
TFA-Twin-Plus-30.3049: e4 43 df 13 a0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -6.8 C / 19.8 F, humidity 44%
TFA-Twin-Plus-30.3049: e4 4a df 93 f0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -7.5 C / 18.5 F, humidity 45%
TFA-Twin-Plus-30.3049: e4 47 5f 93 e0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -8.2 C / 17.2 F, humidity 45%
TFA-Twin-Plus-30.3049: e4 41 5f 53 40
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -8.8 C / 16.2 F, humidity 46%
TFA-Twin-Plus-30.3049: e4 49 9f 33 20
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -10.3 C / 13.5 F, humidity 48%
TFA-Twin-Plus-30.3049: e4 41 1f b3 c0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -12.0 C / 10.4 F, humidity 49%
TFA-Twin-Plus-30.3049: e4 4c 1f 73 f0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 07: battery OK, channel 1, temperature -12.5 C / 9.5 F, humidity 50%
TFA-Twin-Plus-30.3049: 24 01 f0 13 80
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 04: battery OK, channel 1, temperature 24.8 C / 76.6 F, humidity 44%
TFA-Twin-Plus-30.3049: 84 02 08 83 a0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 01: battery OK, channel 1, temperature 26.0 C / 78.8 F, humidity 37%
TFA-Twin-Plus-30.3049: 96 02 08 83 80
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 19: battery OK, channel 1, temperature 26.0 C / 78.8 F, humidity 37%
TFA-Twin-Plus-30.3049: c4 0c 08 83 60
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 03: battery OK, channel 1, temperature 25.9 C / 78.6 F, humidity 37%
TFA-Twin-Plus-30.3049: 46 0c 08 83 90
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 12: battery OK, channel 1, temperature 25.9 C / 78.6 F, humidity 37%
TFA-Twin-Plus-30.3049: 75 0c 08 83 90
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 2e: battery OK, channel 1, temperature 25.9 C / 78.6 F, humidity 37%
TFA-Twin-Plus-30.3049: 75 04 08 03 e0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 2e: battery OK, channel 1, temperature 25.8 C / 78.4 F, humidity 36%
TFA-Twin-Plus-30.3049: 96 8e 88 c3 10
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 19: battery low, channel 1, temperature 27.9 C / 82.2 F, humidity 39%
TFA-Twin-Plus-30.3049: 96 81 88 83 e0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 19: battery low, channel 1, temperature 28.0 C / 82.4 F, humidity 37%
TFA-Twin-Plus-30.3049: 96 86 88 83 a0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 19: battery low, channel 1, temperature 27.8 C / 82.0 F, humidity 37%
TFA-Twin-Plus-30.3049: a8 02 48 c3 30
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 05: battery OK, channel 2, temperature 29.2 C / 84.6 F, humidity 39%
TFA-Twin-Plus-30.3049: a8 40 88 83 e0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 05: battery OK, channel 2, temperature 27.2 C / 81.0 F, humidity 37%
TFA-Twin-Plus-30.3049: 88 05 08 83 50
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 01: battery OK, channel 2, temperature 26.6 C / 79.9 F, humidity 37%
TFA-Twin-Plus-30.3049: 0b 09 08 43 a0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 30: battery OK, channel 2, temperature 26.5 C / 79.7 F, humidity 38%
TFA-Twin-Plus-30.3049: 5b 0a 08 83 50
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 3a: battery OK, channel 2, temperature 26.1 C / 79.0 F, humidity 37%
TFA-Twin-Plus-30.3049: 7a 0c 08 83 20
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 1e: battery OK, channel 2, temperature 25.9 C / 78.6 F, humidity 37%
TFA-Twin-Plus-30.3049: da 04 08 83 00
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 1b: battery OK, channel 2, temperature 25.8 C / 78.4 F, humidity 37%
TFA-Twin-Plus-30.3049: b8 08 08 83 b0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 0d: battery OK, channel 2, temperature 25.7 C / 78.3 F, humidity 37%
TFA-Twin-Plus-30.3049: fb 08 08 83 d0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 3f: battery OK, channel 2, temperature 25.7 C / 78.3 F, humidity 37%
TFA-Twin-Plus-30.3049: 68 00 08 83 a0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 06: battery OK, channel 2, temperature 25.6 C / 78.1 F, humidity 37%
TFA-Twin-Plus-30.3049: ea 00 08 83 50
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 17: battery OK, channel 2, temperature 25.6 C / 78.1 F, humidity 37%
TFA-Twin-Plus-30.3049: 69 00 08 83 b0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 26: battery OK, channel 2, temperature 25.6 C / 78.1 F, humidity 37%
TFA-Twin-Plus-30.3049: f8 00 08 83 70
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 0f: battery OK, channel 2, temperature 25.6 C / 78.1 F, humidity 37%
TFA-Twin-Plus-30.3049: e9 0f f0 83 d0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 27: battery OK, channel 2, temperature 25.5 C / 77.9 F, humidity 37%
TFA-Twin-Plus-30.3049: 1b 0f f0 83 00
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 38: battery OK, channel 2, temperature 25.5 C / 77.9 F, humidity 37%
TFA-Twin-Plus-30.3049: 98 0f f0 83 a0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 09: battery OK, channel 2, temperature 25.5 C / 77.9 F, humidity 37%
TFA-Twin-Plus-30.3049: 4b 07 f0 83 90
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 32: battery OK, channel 2, temperature 25.4 C / 77.7 F, humidity 37%
TFA-Twin-Plus-30.3049: ca 07 f0 83 40
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 13: battery OK, channel 2, temperature 25.4 C / 77.7 F, humidity 37%
TFA-Twin-Plus-30.3049: 8b 07 f0 83 10
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 31: battery OK, channel 2, temperature 25.4 C / 77.7 F, humidity 37%
TFA-Twin-Plus-30.3049: da 07 f0 83 50
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 1b: battery OK, channel 2, temperature 25.4 C / 77.7 F, humidity 37%
TFA-Twin-Plus-30.3049: 29 07 f0 83 e0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 24: battery OK, channel 2, temperature 25.4 C / 77.7 F, humidity 37%
TFA-Twin-Plus-30.3049: 8b 00 08 43 b0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 31: battery OK, channel 2, temperature 25.6 C / 78.1 F, humidity 38%
TFA-Twin-Plus-30.3049: 2b 0f f0 83 30
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 34: battery OK, channel 2, temperature 25.5 C / 77.9 F, humidity 37%
TFA-Twin-Plus-30.3049: 59 07 f0 83 b0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 2a: battery OK, channel 2, temperature 25.4 C / 77.7 F, humidity 37%
TFA-Twin-Plus-30.3049: aa 0a 88 63 c0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 15: battery OK, channel 2, temperature 27.7 C / 81.9 F, humidity 42%
TFA-Twin-Plus-30.3049: 38 08 08 43 b0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 0c: battery OK, channel 2, temperature 25.7 C / 78.3 F, humidity 38%
TFA-Twin-Plus-30.3049: ae 08 88 c3 70
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 15: battery OK, channel 3, temperature 27.3 C / 81.1 F, humidity 39%
TFA-Twin-Plus-30.3049: 4f 0d 08 83 50
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 32: battery OK, channel 3, temperature 26.7 C / 80.1 F, humidity 37%
TFA-Twin-Plus-30.3049: be 05 08 83 30
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 1d: battery OK, channel 3, temperature 26.6 C / 79.9 F, humidity 37%
TFA-Twin-Plus-30.3049: 3e 01 08 83 90
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 1c: battery OK, channel 3, temperature 26.4 C / 79.5 F, humidity 37%
TFA-Twin-Plus-30.3049: bd 01 08 83 70
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 2d: battery OK, channel 3, temperature 26.4 C / 79.5 F, humidity 37%
TFA-Twin-Plus-30.3049: ac 0e 08 83 b0
2015-08-31 22:37:01 TFA-Twin-Plus-30.3049 Sensor 05: battery OK, channel 3, temperature 26.3 C / 79.3 F, humidity 37%
Passed 74/74 positive tests
 */

#endif

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "channel",
    "battery",
    "temperature_C",
    "humidity",
    NULL
};

r_device tfa_twin_plus_303049 = {
  .name          = "TFA-Twin-Plus-30.3049 and Ea2 BL999",
  .modulation    = OOK_PULSE_PPM_RAW,
  .short_limit   = 2800,
  .long_limit    = 8000,
  .reset_limit   = 8000,
  .json_callback = &tfa_twin_plus_303049_callback,
  .disabled      = 0,
  .demod_arg     = 0,
  .fields         = output_fields
};
