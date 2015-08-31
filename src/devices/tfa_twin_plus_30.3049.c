#include "rtl_433.h"
#include "util.h"

/*
 * TFA-Twin-Plus-30.3049
 *
 * Copyright (C) 2015 Paul Ortyl
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

inline static int reverse_bits(int bits, int length)
{
  int tmp = 0;
  for(int i=0; i<length; i++)
    tmp = (tmp << 1) | ((bits >> i) & 1);

  return tmp;
}

static int tfa_twin_plus_303049_process_row(int row, const bitbuffer_t *bitbuffer)
{
   const uint8_t *b = bitbuffer->bb[row];
  const uint16_t length = bitbuffer->bits_per_row[row];

  if (36 != length)
    return 0;

  //const uint8_t b[] = {0xe4, 0x4b, 0x70, 0x73, 0x00};
  fprintf(stderr, "TFA-Twin-Plus-30.3049: %02x %02x %02x %02x %02x\n", b[0], b[1], b[2], b[3], b[4]);


  const int negative_sign = (b[2] & 7);

  if (negative_sign != 0 && negative_sign != 7)
    return 0;

  const int temp     = reverse_bits(((b[1] & 0xF) << 5) | ((b[2] >> 3) & 0x1F), 9);
  const int humidity = reverse_bits((b[3]>>1) & 0x7F, 7) - 28;
  const int sensor_id      =  (b[0] << 4) | ((b[1] >>4) & 0xF);

  float tempC = (negative_sign ? ( (1<<9) - temp ) : temp ) * 0.1F;
  {
    /* @todo: remove timestamp printing as soon as the controller takes this task */
    time_t time_now;
    char time_str[LOCAL_TIME_BUFLEN];
    time(&time_now);
    local_time_str(time_now, time_str);

    /* @todo make temperature unit configurable, not printing both */
    fprintf(stdout, "%s TFA-Twin-Plus-30.3049 Sensor %02x: temperature %3.1f C / %3.1f F, humidity %2d%%\n"
            , time_str, sensor_id, tempC, celsius2fahrenheit(tempC), humidity
    );
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


r_device tfa_twin_plus_303049 = {
  .name          = "TFA-Twin-Plus-30.3049",
  .modulation    = OOK_PULSE_PPM_RAW,
  .short_limit   = 700,
  .long_limit    = 2000,
  .reset_limit   = 2000,
  .json_callback = &tfa_twin_plus_303049_callback,
  .disabled      = 0,
  .demod_arg     = 0,
};
