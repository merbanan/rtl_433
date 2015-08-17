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



static int tfa_twin_plus_303049_process_row(int row, const bitbuffer_t *bitbuffer)
{
  const uint8_t *b = bitbuffer->bb[row];
  const uint16_t length = bitbuffer->bits_per_row[row];

  if (36 != length)
    return 0;

#if 0
  {
    time_t time_now;
    char time_str[LOCAL_TIME_BUFLEN];
    time(&time_now);
    local_time_str(time_now, time_str);
    fprintf(stdout, "%s Brennstuhl RCS 2044: system code: %d%d%d%d%d. key: %c, state: %s\n",
      time_str,
      system_code[0], system_code[1], system_code[2], system_code[3], system_code[4],
      key,
      on ? "ON" : ( off ? "OFF" : "BOTH" ) /* "BOTH" is excluded above, but leave it here for debug purposes */
    );
  }
#endif

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
  .short_limit   = 7000,
  .long_limit    = 3000,
  .reset_limit   = 3000,
  .json_callback = &tfa_twin_plus_303049_callback,
  .disabled      = 0,
  .demod_arg     = 0,
};
