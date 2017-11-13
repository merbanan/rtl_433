/* wt450 wireless weather sensors protocol
 *
 * Tested devices:
 * WT260H
 * WT405H
 *
 * Copyright (C) 2015 Tommy Vestermark
 * Copyright (C) 2015 Ladislav Foldyna
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 source from:
 http://ala-paavola.fi/jaakko/doku.php?id=wt450h


 The signal is FM encoded with clock cycle around 2000 µs
 No level shift within the clock cycle translates to a logic 0
 One level shift within the clock cycle translates to a logic 1
 Each clock cycle begins with a level shift
 My timing constants defined below are those observed by my program

 +---+   +---+   +-------+       +  high
 |   |   |   |   |       |       |
 |   |   |   |   |       |       |
 +   +---+   +---+       +-------+  low
 ^       ^       ^       ^       ^  clock cycle
 |   1   |   1   |   0   |   0   |  translates as

 Each transmission is 36 bits long (i.e. 72 ms)

 Data is transmitted in pure binary values, NOT BCD-coded.
*/


/*
 * Outdoor sensor transmits data temperature, humidity.
 * Transmissions also include channel code and house code. The sensor transmits
 * every 60 seconds 3 packets.
 *
 * 1100 0001 | 0011 0011 | 1000 0011 | 1011 0011 | 0001
 * xxxx ssss | ccxx bhhh | hhhh tttt | tttt tttt | tttp
 *
 * x - constant
 * s - House code
 * c - Channel
 * b - battery indicator (0=>OK, 1=>LOW)
 * h - Humidity
 * t - Temperature
 * p - parity (xor of all bits should give 0)
 */

#include "rtl_433.h"
#include "util.h"
#include "pulse_demod.h"
#include "data.h"

static int wt450_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    uint8_t *b = bb[0];

    uint8_t humidity = 0;
    uint8_t temp_whole = 0;
    uint8_t temp_fraction = 0;
    uint8_t house_code = 0;
    uint8_t channel = 0;
    uint8_t battery_low = 0;
    float temp = 0;
    uint8_t bit;
    uint8_t parity = 0;

    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    local_time_str(0, time_str);

//bitbuffer_print(bitbuffer);

    if ( bitbuffer->bits_per_row[0] != 36 )
    {
        if (debug_output)
            fprintf(stderr, "%s wt450_callback: wrong size of bit per row %d\n",
                    time_str, bitbuffer->bits_per_row[0]);

        return 0;
    }

    if ( b[0]>>4 != 0xC )
    {
        if (debug_output)
        {
            fprintf(stderr, "%s wt450_callback: wrong preamble\n", time_str);
            bitbuffer_print(bitbuffer);
        }
        return 0;
    }

    for ( bit = 0; bit < bitbuffer->bits_per_row[0]; bit++ )
    {
        parity ^= (b[bit/8] & (0x80 >> (bit % 8))) ? 1 : 0;
    }

    if ( parity )
    {
        if (debug_output)
        {
            fprintf(stderr, "%s wt450_callback: wrong parity\n", time_str);
            bitbuffer_print(bitbuffer);
        }
        return 0;
    }

    house_code = b[0] & 0xF;
    channel = (b[1] >> 6) + 1;
    battery_low = b[1] & 0x8;
    humidity = ((b[1] & 0x7) << 4) + (b[2] >> 4);
    temp_whole = (b[2] << 4) + (b[3] >> 4);
    temp_fraction = ((b[3] & 0xF) << 3) + (b[4] >> 5);
    temp = (temp_whole - 50) + (temp_fraction/100.0);

    data = data_make("time",          "",  DATA_STRING, time_str,
        "model",         "",	   DATA_STRING, "WT450 sensor",
        "id",            "House Code", DATA_INT, house_code,
        "channel",       "Channel",    DATA_INT, channel,
        "battery",       "Battery",    DATA_STRING, battery_low ? "LOW" : "OK",
        "temperature_C", "Temperature",DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp,
        "humidity",      "Humidity",   DATA_FORMAT, "%u %%", DATA_INT, humidity,
        NULL);
    data_acquired_handler(data);

    return 1;
}

PWM_Precise_Parameters clock_bits_parameters_generic = {
    .pulse_tolerance	= 80, // us
    .pulse_sync_width	= 0,	// No sync bit used
};

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

r_device wt450 = {
    .name          = "WT450",
    .modulation    = OOK_PULSE_CLOCK_BITS,
    .short_limit   = 980,
    .long_limit    = 1952,
    .reset_limit   = 18000,
    .json_callback = &wt450_callback,
    .disabled      = 0,
    .demod_arg     = (uintptr_t)&clock_bits_parameters_generic,
    .fields        = output_fields
};
