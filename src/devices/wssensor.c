/*
 * Hyundai WS SENZOR Remote Temperature Sensor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Transmit Interval: every ~33s
 * Frequency 433.92 MHz
 * Distance coding: Pulse length 224 us
 * Short distance: 1032 us, long distance: 1992 us, packet distance: 4016 us
 *
 * 24-bit data packet format, repeated 23 times
 *   TTTTTTTT TTTTBSCC IIIIIIII
 *
 *   T = signed temperature * 10 in Celsius
 *   B = battery status (0 = low, 1 = OK)
 *   S = startup (0 = normal operation, 1 = battery inserted or TX button pressed)
 *   C = channel (0-2)
 *   I = sensor ID
 */

#include "rtl_433.h"
#include "data.h"
#include "util.h"

#define MODEL "WS Temperature Sensor"
#define WS_PACKETLEN	24
#define WS_MINREPEATS	4
#define WS_REPEATS	23

static int wssensor_callback(bitbuffer_t *bitbuffer) {
    uint8_t *b;
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];

    // the signal should have 23 repeats
    // require at least 4 received repeats
    int r = bitbuffer_find_repeated_row(bitbuffer, WS_MINREPEATS, WS_REPEATS);
    if (r < 0 || bitbuffer->bits_per_row[r] != WS_PACKETLEN)
        return 0;

    b = bitbuffer->bb[r];

    int16_t temperature;
    uint8_t battery_status;
    uint8_t startup;
    uint8_t channel;
    uint8_t sensor_id;
    float temperature_c;

    /* TTTTTTTT TTTTBSCC IIIIIIII  */
    temperature = (int16_t)(((int8_t)(b[0] & 0xFFu) << 4) | ((b[1] & 0xF0u) >> 4));
    battery_status = (uint8_t)((b[1] >> 3) & 1);
    startup = (uint8_t)((b[1] >> 2) & 1);
    channel = (uint8_t)(b[1] & 3) + 1;
    sensor_id = (uint8_t)b[2];

    temperature_c = temperature / 10.0f;

    if (debug_output) {
        fprintf(stdout, "Hyundai WS SENZOR received raw data:\n");
        bitbuffer_print(bitbuffer);
        fprintf(stdout, "Sensor ID	= %01d = 0x%02x\n",  sensor_id, sensor_id);
        fprintf(stdout, "Bitstream HEX	= %02x %02x %02x\n", b[0], b[1], b[2]);
        fprintf(stdout, "Battery OK	= %0d\n", battery_status);
        fprintf(stdout, "Startup		= %0d\n", startup);
        fprintf(stdout, "Channel		= %0d\n", channel);
        fprintf(stdout, "temp		= %d = 0x%02x\n", temperature, temperature);
        fprintf(stdout, "TemperatureC	= %.1f\n", temperature_c);
    }

    local_time_str(0, time_str);
    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, MODEL,
                     "id",            "House Code",  DATA_INT, sensor_id,
                     "channel",       "Channel",     DATA_INT, channel,
                     "battery",       "Battery",     DATA_STRING, battery_status ? "OK" : "LOW",
                     "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temperature_c,
                     NULL);

    data_acquired_handler(data);
    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "channel",
    "battery",
    "temperature_C",
    NULL
};

/*
Analyzing pulses...
Total count:  575,  width: 236925		(947.7 ms)
Pulse width distribution:
 [ 0] count:  575,  width:    56 [53;65]	( 224 us)
Gap width distribution:
 [ 0] count:  390,  width:   258 [252;289]	(1032 us)
 [ 1] count:  161,  width:   498 [497;533]	(1992 us)
 [ 2] count:   23,  width:  1004 [1003;1005]	(4016 us)
Pulse period distribution:
 [ 0] count:  390,  width:   315 [311;346]	(1260 us)
 [ 1] count:  161,  width:   556 [554;591]	(2224 us)
 [ 2] count:   23,  width:  1061 [1060;1063]	(4244 us)
Level estimates [high, low]:  15905,    100
Frequency offsets [F1, F2]:  -10073,      0	(-38.4 kHz, +0.0 kHz)
*/

r_device wssensor = {
    .name           = MODEL,
    .modulation     = OOK_PULSE_PPM_RAW,
    // note that these are in microseconds, not samples.
    .short_limit    = 1400,
    .long_limit     = 2400,
    .reset_limit    = 4400,
    .json_callback  = &wssensor_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields
};

