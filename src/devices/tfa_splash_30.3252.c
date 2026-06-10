/** @file
    Decoder for TFA Dostmann Rain Gauge (Splash) 30.3252.01.
    Also sold as a kit with the receiver as Andersson Rain Gauge in Sweden by NetOnNet.
    Partly based on tfa_drop_30.3233.c

    Copyright (C) 2026 Peter Eriksson
    Copyright (C) 2020 Michael Haas (tfa_drop_30.3233.c)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/**
TFA Dostmann Splash/Andersson Rain Gauge is a rain gauge with a tipping bucket
mechanism and built in temperature sensor.

Links:

 - Product page:
   - https://www.tfa-dostmann.de/en/produkt/wireless-rain-gauge-drop/
   - https://www.netonnet.se/art/hem-fritid/klimat-varme/vaderstation/andersson-rain-gauge/1011313.16143

The sensor has TFA part number 30.3252.01. The full package, including the
base station, has TFA part number 47.3006.01.

The Andersson @ NetOnNet full package has Netonnet part number 1011313.

The device uses PWM encoding:

- 0 is encoded as 240 us pulse and a 480us gap
- 1 is encoded as 480 us pulse and a 240us gap

Note that this encoding scheme is inverted relative to the default
interpretation of short/long pulses in the PWM decoder in rtl_433.
The implementation below thus inverts the buffer. The protocol is
described below in the correct space, i.e. after the buffer has been
inverted.

Not every tip of the bucket triggers a message immediately. In some
cases, artificially tipping the bucket many times lead to the base
station ignoring the signal completely until the device was reset.

Data layout:

```
IIIIIIII IIIIIIII BCXXXXXX RRRRRRRR SSSSSSS TTTTTTTT UUUUUUU MMMMMMMM
KKKK
```


- I: 2 byte ID, randomly selected at power-on
- B: 1 bit, battery_low. 0 if battery OK, 1 if battery is low.
- C: 1 bit, device reset. Set to 1 briefly after battery insert.
- X: 6 bit, unknown, always 0?
- R: LSB of 16-bit little endian rain counter raw valuie
- S: MSB of 16-bit little endian rain counter raw valuie
- T: LSB of 16-bit little endian temperature raw valuie
- U: MSB of 16-bit little endian temperature raw valuie
- M: Checksum. Simple sum of bytes 0-7
- K: Unknown. 


The rain bucket counter represents the number of tips of the rain
bucket. Each tip of the bucket corresponds to 0.254mm of rain.

The rain bucket counter does not start at 0. Instead, the counter
starts at 65535-120 = 65416 to indicate 0 tips of the bucket. The counter rolls
over at 65535 to 0.

If no change is detected, the sensor will continue broadcasting
identical values

After battery insertion, the sensor will transmit 5 messages in rapid
succession, one message every 3 seconds. These will have B=0 and C=1.
Then after 3 more seconds normal transmission starts. These will have C=0 and
depending on battery status may or may not have B set to 1. The X bits are unknown.
Normal messages will be transmitted every 45s.

Some example data:

```
Bad battery:
e7 a1 40 88 ff 74 06 c9 : 1110 0111  1010 0001  0100 0000  1000 1000  1111 1111  0111 0100  0000 0110  1100 1001
e7 a1 40 88 ff 74 06 c9 : 1110 0111  1010 0001  0100 0000  1000 1000  1111 1111  0111 0100  0000 0110  1100 1001
e7 a1 40 88 ff 74 06 c9 : 1110 0111  1010 0001  0100 0000  1000 1000  1111 1111  0111 0100  0000 0110  1100 1001
e7 a1 40 88 ff 74 06 c9 : 1110 0111  1010 0001  0100 0000  1000 1000  1111 1111  0111 0100  0000 0110  1100 1001
e7 a1 40 88 ff 74 06 c9 : 1110 0111  1010 0001  0100 0000  1000 1000  1111 1111  0111 0100  0000 0110  1100 1001
e7 a1 80 88 ff 74 06 09 : 1110 0111  1010 0001  1000 0000  1000 1000  1111 1111  0111 0100  0000 0110  0000 1001
e7 a1 80 88 ff 72 06 07 : 1110 0111  1010 0001  1000 0000  1000 1000  1111 1111  0111 0010  0000 0110  0000 0111
e7 a1 80 88 ff 76 06 0b : 1110 0111  1010 0001  1000 0000  1000 1000  1111 1111  0111 0110  0000 0110  0000 1011

Good battery:
f1 f6 40 88 ff 79 06 2d : 1111 0001  1111 0110  0100 0000  1000 1000  1111 1111  0111 1001  0000 0110  0010 1101
f1 f6 40 88 ff 79 06 2d : 1111 0001  1111 0110  0100 0000  1000 1000  1111 1111  0111 1001  0000 0110  0010 1101
f1 f6 40 88 ff 79 06 2d : 1111 0001  1111 0110  0100 0000  1000 1000  1111 1111  0111 1001  0000 0110  0010 1101
f1 f6 40 88 ff 79 06 2d : 1111 0001  1111 0110  0100 0000  1000 1000  1111 1111  0111 1001  0000 0110  0010 1101
f1 f6 40 88 ff 79 06 2d : 1111 0001  1111 0110  0100 0000  1000 1000  1111 1111  0111 1001  0000 0110  0010 1101
f1 f6 00 88 ff 79 06 ed : 1111 0001  1111 0110  0000 0000  1000 1000  1111 1111  0111 1001  0000 0110  1110 1101
f1 f6 00 88 ff 78 06 ec : 1111 0001  1111 0110  0000 0000  1000 1000  1111 1111  0111 1000  0000 0110  1110 1100

Restart (pull battery and insert again, unit 1):
----  ID ID FL RC RC TC TC MI   ID------------------  FLAGS----  RAIN----------------  TEMP----------------  MIC------
3a 80 40 88 ff 7f 06 06 : 0011 1010  1000 0000  0100 0000  1000 1000  1111 1111  0111 1111  0000 0110  0000 0110
8c e3 40 88 ff 7f 06 bb : 1000 1100  1110 0011  0100 0000  1000 1000  1111 1111  0111 1111  0000 0110  1011 1011
1b 33 40 88 ff 80 06 9b : 0001 1011  0011 0011  0100 0000  1000 1000  1111 1111  1000 0000  0000 0110  1001 1011
5a a4 40 88 ff 7f 06 4a : 0101 1010  1010 0100  0100 0000  1000 1000  1111 1111  0111 1111  0000 0110  0100 1010
55 93 40 88 ff 7f 06 34 : 0101 0101  1001 0011  0100 0000  1000 1000  1111 1111  0111 1111  0000 0110  0011 0100
5e 95 40 88 ff 7f 06 3f : 0101 1110  1001 0101  0100 0000  1000 1000  1111 1111  0111 1111  0000 0110  0011 1111
6a 8f 40 88 ff 7f 06 45 : 0110 1010  1000 1111  0100 0000  1000 1000  1111 1111  0111 1111  0000 0110  0100 0101
d4 e5 40 88 ff 7f 06 05 : 1101 0100  1110 0101  0100 0000  1000 1000  1111 1111  0111 1111  0000 0110  0000 0101
4a c1 40 88 ff 7f 06 57 : 0100 1010  1100 0001  0100 0000  1000 1000  1111 1111  0111 1111  0000 0110  0101 0111
bc 37 40 88 ff 83 06 43 : 1011 1100  0011 0111  0100 0000  1000 1000  1111 1111  1000 0011  0000 0110  0100 0011

Restart (pull battery and insert again, unit 2):
f8 22 40 88 ff 64 06 4b : 1111 1000  0010 0010  0100 0000  1000 1000  1111 1111  0110 0100  0000 0110  0100 1011
f8 22 40 89 ff 64 06 4c : 1111 1000  0010 0010  0100 0000  1000 1001  1111 1111  0110 0100  0000 0110  0100 1100
c5 f5 40 88 ff 63 06 ea : 1100 0101  1111 0101  0100 0000  1000 1000  1111 1111  0110 0011  0000 0110  1110 1010
c5 99 40 88 ff 63 06 8e : 1100 0101  1001 1001  0100 0000  1000 1000  1111 1111  0110 0011  0000 0110  1000 1110
c0 39 40 88 ff 63 06 29 : 1100 0000  0011 1001  0100 0000  1000 1000  1111 1111  0110 0011  0000 0110  0010 1001
8c af 40 88 ff 63 06 6b : 1000 1100  1010 1111  0100 0000  1000 1000  1111 1111  0110 0011  0000 0110  0110 1011
0f 0f 40 88 ff 63 06 4e : 0000 1111  0000 1111  0100 0000  1000 1000  1111 1111  0110 0011  0000 0110  0100 1110
4a a5 40 88 ff 63 06 1f : 0100 1010  1010 0101  0100 0000  1000 1000  1111 1111  0110 0011  0000 0110  0001 1111

```

*/

#include "decoder.h"
#include <time.h>
#define TFA_SPLASH_BITLEN 66
#define TFA_SPLASH_MINREPEATS 2

static int f_debug = 0;

static void print_binary_byte(unsigned char v,
			      FILE *fp) {
  int i;
  
  for (i = 7; i >= 0; i--) {
    putc('0'+( (v & (1<<i)) ? 1 : 0), fp);
    if (i == 4)
      putc(' ', fp);
  }
}

static int tfa_splash_303252_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer); 

    int row_index = bitbuffer_find_repeated_row(bitbuffer, TFA_SPLASH_MINREPEATS,
            TFA_SPLASH_BITLEN);
    if (row_index < 0 || bitbuffer->bits_per_row[row_index] > TFA_SPLASH_BITLEN + 16) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *row_data = bitbuffer->bb[row_index];

    if (f_debug) {
      int i;
      time_t bt;
      static time_t obt = 0;;
      
      time(&bt);
      if (!obt)
	obt = bt;
      
      fprintf(stderr, "%.1f : DATA:", difftime(bt, obt));
      obt = bt;
      for (i = 0; i < 8; i++) {
	fprintf(stderr, " %02x", row_data[i]);
      }
      fprintf(stderr, " :");
      for (i = 0; i < 8; i++) {
	putc(' ', stderr);
	print_binary_byte(row_data[i], stderr);
	putc(' ', stderr);
      }

      putc('\n', stderr);
    }
    
    /*
     * Validate checksum
     */
    uint8_t observed_checksum = row_data[7];
    uint8_t computed_checksum = 0;
    {
      int i;
      for (i = 0; i < 7; i++) 
	computed_checksum += row_data[i];
    }
    if (observed_checksum != computed_checksum) {
      if (f_debug)
        fprintf(stderr, "Wrong checksum: %02x vs %02x\n",
		observed_checksum, computed_checksum);
	
        return DECODE_FAIL_MIC;
    }
    
    int starting = (row_data[2] & 0x40) >> 6;

    if (starting) /* Ignore returned data while under startup */
      return DECODE_ABORT_EARLY;
    
    uint16_t temp_raw = ((uint16_t) row_data[5])|(uint16_t) row_data[6]<<8;
    float temp_c = (temp_raw-1221)*0.0556f;

    if (f_debug)
      fprintf(stderr, "Temperature: %f (%d, 0x%04x)\n", temp_c, temp_raw, temp_raw);
    
    int sensor_id = (row_data[0] << 8) | row_data[1];

    uint16_t rain_counter = row_data[4] << 8 | row_data[3];

    if (f_debug)
      fprintf(stderr, "RAIN counter: %d\n", rain_counter);
    
    rain_counter += 120; /* Start at 0 */
    
    float rain_mm = rain_counter * 0.254f;
    int battery_low = (row_data[2] & 0x80) >> 7;

    /* clang-format off */
    data_t *data = data_make(
            "model",      "",           DATA_STRING, "TFA-SPLASH",
            "id",         "",           DATA_FORMAT, "%5x", DATA_INT,  sensor_id,
            "battery_ok", "Battery",    DATA_INT,    !battery_low,
            "rain_mm",    "Rain total", DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_mm,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "mic",        "Integrity",  DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "rain_mm",
	"temperature_C",
        "mic",
        NULL,
};

r_device const tfa_splash_303252 = {
        .name        = "TFA Splash Rain Gauge 30.3252.01",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 240,
        .long_width  = 480,
        .gap_limit   = 1300,
        .reset_limit = 2500,
        .sync_width  = 750,
        .decode_fn   = &tfa_splash_303252_decode,
        .fields      = output_fields,
};

