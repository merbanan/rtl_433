/** @file
    Hyundai WS SENZOR Remote Temperature Sensor.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Hyundai WS SENZOR Remote Temperature Sensor.

- Transmit Interval: every ~33s
- Frequency 433.92 MHz
- Distance coding: Pulse length 224 us
- Short distance: 1032 us, long distance: 1992 us, packet distance: 4016 us

24-bit data packet format, repeated 23 times

    TTTTTTTT TTTTBSCC IIIIIIII

- T = signed temperature * 10 in Celsius
- B = battery status (0 = low, 1 = OK)
- S = startup (0 = normal operation, 1 = battery inserted or TX button pressed)
- C = channel (0-2)
- I = sensor ID
*/

#include "decoder.h"

#define WS_PACKETLEN 24
#define WS_MINREPEATS 4
#define WS_REPEATS 23

static int wssensor_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b;
    data_t *data;

    // the signal should have 23 repeats
    // require at least 4 received repeats
    int r = bitbuffer_find_repeated_row(bitbuffer, WS_MINREPEATS, WS_REPEATS);
    if (r < 0 || bitbuffer->bits_per_row[r] != WS_PACKETLEN)
        return DECODE_ABORT_LENGTH;

    b = bitbuffer->bb[r];

    // No need to decode/extract values for simple test
    if ((!b[0] && !b[1] && !b[2])
       || (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff)) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: DECODE_FAIL_SANITY data all 0x00 or 0xFF\n", __func__);
        }
        return DECODE_FAIL_SANITY;
    }

    int temperature;
    int battery_status;
    int startup;
    int channel;
    int sensor_id;
    float temperature_c;

    /* TTTTTTTT TTTTBSCC IIIIIIII  */
    temperature = (int16_t)((b[0] << 8) | (b[1] & 0xf0)); // uses sign extend
    battery_status = (b[1] & 0x08) >> 3;
    startup = (b[1] & 0x04) >> 2;
    channel = (b[1] & 0x03) + 1;
    sensor_id = b[2];

    temperature_c = (temperature >> 4) * 0.1f;

    if (decoder->verbose) {
        fprintf(stderr, "Hyundai WS SENZOR received raw data:\n");
        bitbuffer_print(bitbuffer);
        fprintf(stderr, "Sensor ID = %01d = 0x%02x\n",  sensor_id, sensor_id);
        fprintf(stderr, "Bitstream HEX = ");
        bitrow_print(b, 24);
        fprintf(stderr, "Battery OK = %0d\n", battery_status);
        fprintf(stderr, "Startup  = %0d\n", startup);
        fprintf(stderr, "Channel  = %0d\n", channel);
        fprintf(stderr, "temp  = %d = 0x%02x\n", temperature, temperature);
        fprintf(stderr, "TemperatureC = %.1f\n", temperature_c);
    }

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, _X("Hyundai-WS","WS Temperature Sensor"),
            "id",            "House Code",  DATA_INT, sensor_id,
            "channel",       "Channel",     DATA_INT, channel,
            "battery",       "Battery",     DATA_STRING, battery_status ? "OK" : "LOW",
            "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temperature_c,
            "button",           "Button",       DATA_INT, startup,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "battery",
        "temperature_C",
        "button",
        NULL,
};

r_device wssensor = {
        .name        = "Hyundai WS SENZOR Remote Temperature Sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .gap_limit   = 2400,
        .reset_limit = 4400,
        .decode_fn   = &wssensor_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
