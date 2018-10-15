#include "rtl_433.h"
#include "util.h"
#include "data.h"

/*
 * TFA-30.3211.02
 *
 * Copyright (C) 2018 ionum - projekte
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 
 * 2 Bits "Preamble" 72 Bits (18 nibbles)

 *  Nibble    1   2    3   4    5   6    7   8    9   10   11  12   13  14   15  16   17  18
 *         PP ?HHHhhhh ???????? II??TTTT ttttuuuu ???????? ???????? ???????? ???????? ??????
 *
 *    P = Preamble
 *    H = first digit (only to 7?) humidity
 *    h = second digit humidity
 *    I = Channel
 *    T = first digit temperatur
 *    t = second digit temperatur
 *    u = third digit temperatur
 */

static int tfa_303211_callback (bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b      = bitbuffer->bb[0];
    const uint16_t length = bitbuffer->bits_per_row[0];

    /* length check */
	if (74 != length) {
		if(debug_output) fprintf(stderr,"tfa_303211 wrong size (%i bits)\n",length);
		return 0;
	}

	/* shift preamble */
    int i;
    for (i = 0; i < 9; i++) {
      uint8_t b1 = b[i] << 2;
      uint8_t b2 = (b[i+1] & 0xC0) >> 6;
      b[i] = b1 | b2;
}

    //const int negative_sign = (b[2] & 7);
	if(debug_output) {
		fprintf(stderr,"tfa_303211 temp*10 %i\n",((b[2] & 0x0F) * 10));
		fprintf(stderr,"tfa_303211 temp %i\n",((b[3] & 0xF0) >> 4));
		fprintf(stderr,"tfa_303211 temp/10 %i\n",(b[3] & 0x0F));
	}
    const float temp        = ((b[2] & 0x0F) * 10) + ((b[3] & 0xF0) >> 4) + ((b[3] & 0x0F) *0.1F);
    const int humidity      = ((b[0] &0x70) >> 4) * 10 + (b[1] & 0x0F);
    const int sensor_id     = 0;
    const int battery_low   = b[1] >> 7;
    const int channel       = ((b[2]& 0xC0) >> 6) + 1;

    float tempC = 0;
    {
        char time_str[LOCAL_TIME_BUFLEN];
        local_time_str(0, time_str);

        data = data_make("time",          "",            DATA_STRING, time_str,
            "model",         "",            DATA_STRING, "TFA 30.3211.02",
            "id",            "",            DATA_INT, 0,
            "channel",       "",            DATA_INT, channel,
            "battery",       "Battery",     DATA_STRING, "??",
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp,
            "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
            NULL);
        data_acquired_handler(data);
    }

    return 1;
}

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

r_device tfa_30_3211 = {
    .name          = "TFA 30.3211.02",
    .modulation    = OOK_PULSE_PPM_RAW,
    .short_limit   = 2900,
    .long_limit    = 5000,
    .reset_limit   = 36500,
    .json_callback = &tfa_303211_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields         = output_fields
};
