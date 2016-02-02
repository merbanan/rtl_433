/* HT680 Remote control
 *
 * Copyright (C) 2016 Igor Polovnikov
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"

static int ht680_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;

	for(uint8_t i=0;i < bitbuffer->num_rows+1;i++) {
		if(bitbuffer->bits_per_row[i] == 41){ //Length of packet is 41
			fprintf(stdout,"HT680: ");
			for (uint16_t col = 0; col < (bitbuffer->bits_per_row[i]+7)/8; ++col) {
				fprintf(stderr, "%02x ", bb[i][col]);
			}
			fprintf(stdout,"\n");
			return 1;
		}
	}
	return 0;
}

r_device ht680 = {
  .name          = "HT680 Remote control",
  .modulation    = OOK_PULSE_PWM_RAW,
  .short_limit   = 400,
  .long_limit    = 1100,
  .reset_limit   = 2500,
  .json_callback = &ht680_callback,
  .disabled      = 0,
  .demod_arg     = 0
};
