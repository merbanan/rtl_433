/** @file
    Revolt NC5462 Energy Monitor

    Copyright (C) 2023 Nicolai Hess

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
  revolt energy power meter.
  sends on 433.92 MHz.
 
  Pulse Width Modulation with startbit/delimiter
  Two Modes:
  Normal data mode:
  105 pulses
  first pulse sync
  104 data pulse (11 times 8 bit data + 8 bit checksum + 8 bit unknown)
  11 byte data:
   +----------+----------+
   |* data    |byte      |
   +----------+----------+
   |* id      |0,1       |
   +----------+----------+
   |* voltage |2         |
   +----------+----------+
   |* current |3,4       |
   +----------+----------+
   |*         |5         |
   |frequency |          |
   +----------+----------+
   |* power   |6,7       |
   +----------+----------+
   |* power   |8         |
   |factor    |          |
   +----------+----------+
   |* energy  |9,10      |
   +----------+----------+
   |* detect  |first bit |
   |flag      |high      |
   |          |yes/no    |
   +----------+----------+
   
  
 
  "Register" mode 
  (after pushing button on energy meter)
  same 104 data pulses as in data mode, but first bit high and multiple rows of (the same)
  data.
 
  Pulses
  sync ~ 10 ms high / 280 us low
  1-bit ~ 320 us high / 160 us low
  0-bit ~ 180 us high / 160 us low
  message end 180 us high / 100 ms low
 
  rtl_433 demodulation output
  (normal data)
  short_width: 200, long_width: 330, reset_limit: 240, sync_width: 10044
  (detect flag)
  short_width: 200, long_width: 330, reset_limit: 240, sync_width: 10044
*/

#include "decoder.h"
#include "r_util.h"

static int check_bitbuffer_row1byte12(bitbuffer_t *bitbuffer)
{
  uint8_t checksum;
  uint8_t byte12;
  int index;
  if(bitbuffer->num_rows == 1)
    if(bitbuffer->bits_per_row[0] == 104) {
      checksum = 0;
      byte12 = bitbuffer->bb[0][11];
      for(index=0; index<11;++index) {
	checksum+=bitbuffer->bb[0][index];
      }
      return checksum == byte12;
    }
  return 0;
}

/**
 decode
*/
static int revolt_nc5462_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    data_t *data;
    uint16_t id;
    uint8_t voltage;
    uint16_t current;
    uint8_t frequency;
    uint16_t power;
    uint8_t pf;
    uint16_t energy;
    uint8_t detect_flag;
    bitbuffer_invert(bitbuffer);
    
    if (check_bitbuffer_row1byte12(bitbuffer)) {
      id = (bb[0][0]<<8) | (bb[0][1]);
      if((id & 0x8000) == 0x8000) {
	detect_flag = 1;
	id &= ~0x8000;
      } else {
	detect_flag = 0;
      }
	  
      voltage = bb[0][2];
      current = bb[0][4] | bb[0][3]<<8;
      frequency = bb[0][5];
      power = bb[0][7] | bb[0][6]<<8;
      pf = bb[0][8];
      energy = bb[0][10] | bb[0][9]<<8;
      data = data_make(
                "model", "", DATA_STRING, "NC5462",
                "id", "House Code",  DATA_INT, id,
                "voltage", "Voltage",  DATA_INT, voltage,
                "current", "Current ",DATA_FORMAT, "%.02f A ",  DATA_DOUBLE, .01 * current,
                "frequency", "Frequency",  DATA_INT, frequency,
                "power", "Power",  DATA_FORMAT, "%.02f W",DATA_DOUBLE, .1*power,
                "power factor", "power factor",DATA_FORMAT, "%.02f Pf",  DATA_DOUBLE, .01*pf,
                "energy", "energy",DATA_FORMAT, "%.02f kWh",  DATA_DOUBLE, .01*energy,
                "detect flag", "Detect Flag", DATA_STRING, detect_flag ? "Yes" : "No",
                NULL);
      decoder_output_data(decoder, data);
      return 1;
    }
    return 0;
}

static char* output_fields[] = {
	"time",
	"model",
	"id",
	"voltage",
	"current",
	"frequency",
	"power",
	"power factor",
	"energy",
	"detect flag",
	NULL
};


	/* .short_limit    = 260, */
	/* .long_limit     = 5200, */
	/* .reset_limit    = 288, */

	/* .short_limit    = 68*4, */
	/* .long_limit     = 1297*4, */
	/* .reset_limit    = 61*4, */

r_device revolt_nc5462 = {
	.name           = "Revolt NC-5642",
	.modulation     = OOK_PULSE_PWM,
	.short_width    = 200, 
	.long_width     = 320, 
	.sync_width     = 10024,
	.reset_limit	= 272,
	//.json_callback  = &revolt_nc5462_callback,
	//.demod_arg      = 2,
	.decode_fn      = &revolt_nc5462_callback,
	.fields         = output_fields,
};
