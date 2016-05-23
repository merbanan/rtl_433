#include <bitbuffer.h>
#include "util.h"
#include "data.h"
#include "rtl_433.h"


/*
    Esperanza EWS-103 sensor on 433.92Mhz

    Copyright (C) 2015 Alberts Saulitis
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3 as
    published by the Free Software Foundation.
*/

/*
    No frame description was available on the internet therefore it was required
    to reverse engineer it.
    0        1        2        3        4        5
    AAAABBBB ????CCTT TTTTTTTT TTHHHHHH HH?????? ??

    A - Preamble
    B - Rolling device ID
    C - Channel (1-3)
    T - Temperature (Little-endian)
    H - Humidity (Little-endian)
    ? - Unknown
*/

/*
   Sample Data:
    Esperanze EWS: TemperatureF=55.5 TemperatureC=13.1 Humidity=74 Device_id=0 Channel=1

    *** signal_start = 16189, signal_end = 262145
    signal_len = 245956,  pulses = 266
    Iteration 1. t: 142    min: 141 (37)    max: 143 (229)    delta 4
    Iteration 2. t: 142    min: 141 (2)    max: 143 (264)    delta 0
    Distance coding: Pulse length 142

    Short distance: 487, long distance: 964, packet distance: 1920

    p_limit: 142
    bitbuffer:: Number of rows: 14
    [00] {0} :
    [01] {0} :
    [02] {42} 00 53 e5 69 02 00 : 00000000 01010011 11100101 01101001 00000010 00
    [03] {0} :
    [04] {42} 00 53 e5 69 02 00 : 00000000 01010011 11100101 01101001 00000010 00
    [05] {0} :
    [06] {42} 00 53 e5 69 02 00 : 00000000 01010011 11100101 01101001 00000010 00
    [07] {0} :
    [08] {42} 00 53 e5 69 02 00 : 00000000 01010011 11100101 01101001 00000010 00
    [09] {0} :
    [10] {42} 00 53 e5 69 02 00 : 00000000 01010011 11100101 01101001 00000010 00
    [11] {0} :
    [12] {42} 00 53 e5 69 02 00 : 00000000 01010011 11100101 01101001 00000010 00
    [13] {0} :
    Test mode file issued 4 packets
*/

static int esperanza_ews_process_row(const bitbuffer_t *bitbuffer, int row)
{
    const uint8_t *b = bitbuffer->bb[row];
    uint8_t humidity;
    uint16_t temperature_with_offset;
    uint8_t device_id;
    uint8_t channel;
    float temperature_f;

    data_t *data;

    char time_str[LOCAL_TIME_BUFLEN];

    local_time_str(0, time_str);

    humidity = (uint8_t)((b[3] << 6) | ((b[4] >> 2) & 0x0F) | ((b[3] >>2) & 0xF));
    temperature_with_offset = (uint16_t)(((b[2] << 10) | ((b[3] << 2) & 0x300) | ((b[3] << 2) & 0xF0) | ((b[1] << 2) & 0x0C) |  b[2] >> 6) & 0x0FFF);
    device_id = (uint8_t)(b[0] & 0x0F);
    channel = (uint8_t)((b[1] & 0x0C)+1);
    temperature_f = (float)((temperature_with_offset-900)/10.0);

    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, "Esperanza EWS",
                     "id",            "",            DATA_INT, device_id,
                     "channel",       "Channel",     DATA_INT, channel,
                     "temperature_F", "Temperature", DATA_FORMAT, "%.02f F", DATA_DOUBLE, temperature_f,
                     "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                      NULL);
    data_acquired_handler(data);

    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "channel",
    "temperature_F",
    "humidity",
    NULL
};

static int esperanza_ews_callback(bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows == 14) {
        for (int row=2; row < bitbuffer->num_rows-3; row+=2) {
            if (memcmp(bitbuffer->bb[row], bitbuffer->bb[row+2], sizeof(bitbuffer->bb[row])) != 0 || bitbuffer->bits_per_row[row] != 42) return 0;
        }
        esperanza_ews_process_row(bitbuffer, 2);
    }
    return 1;
}


r_device esperanza_ews = {
        .name          = "Esperanza EWS",
        .modulation    = OOK_PULSE_PPM_RAW,
        .short_limit   = 2800,
        .long_limit    = 4400,
        .reset_limit   = 8000,
        .json_callback = &esperanza_ews_callback,
        .disabled      = 0,
        .demod_arg     = 0,
};
