/* WT0124 Pool Thermometer decoder.
 *
 * Copyright (C) 2018 Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 */


#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"


static int wt1024_callback(bitbuffer_t *bitbuffer)
{
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    uint8_t *b; // bits of a row
    uint16_t sensor_rid;
    uint8_t msg_type;
    int16_t value;
    float temp_c;
    uint8_t channel;

    if (bitbuffer->bits_per_row[1] !=49)
        return 0;


    /* select row after preamble */
    b = bitbuffer->bb[1];

    /* Validate constant */
    if (b[0]>>4 != 0x5) {
        return 0;
    }

    /* Get rid */
    sensor_rid = (b[0]&0x0F)<<4 | (b[1]&0x0F);

    /* Get temperature */
    temp_c = (float) ((((b[1]&0xF)<<8) | b[2])-0x990) / 10.0;

    /* Get channel */
    channel = ((b[4]>>4) & 0x3) + 1;

    /* crc? */
    value = (b[5]<<8)| b[6]; 

    if (debug_output) {
        fprintf(stderr, "wt1024_callback:");
        bitbuffer_print(bitbuffer);
    }
    local_time_str(0, time_str);

    data = data_make(
            "time",  "", DATA_STRING, time_str,
            "model", "", DATA_STRING, "WT0124 Pool Thermometer",
            "rid",    "Random ID", DATA_INT,    sensor_rid,
            "channel",       "Channel",     DATA_INT,    channel,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "data",  "", DATA_INT,    value,
            NULL);

    data_acquired_handler(data);

    // Return 1 if message successfully decoded
    return 1;
}

/*
 * List of fields that may appear in the output
 *
 * Used to determine what fields will be output in what
 * order for this device when using -F csv.
 *
 */
static char *output_fields[] = {
    "time",
    "model",
    "rid",
    "channel",
    "temperature_C",
    "data",
    NULL
};


r_device wt1024 = {
    .name          = "WT0124 Pool Thermometer",
    .modulation    = OOK_PULSE_PWM_PRECISE,
    .short_limit   = 680,
    .long_limit    = 1850,
    .reset_limit   = 10000,
    .gap_limit     = 4000,
    .json_callback = &wt1024_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = output_fields,
};
