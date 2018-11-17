/* TFA 30.3211.02 
 * Copyright (C) 2018 ionum - projekte
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * 
 * 1970us pulse with variable gap (third pulse 3920 us)
 * Above 79% humidity, gap after third pulse is 5848 us
 * 
 * Bit 1 : 1970us pulse with 3888 us gap
 * Bit 0 : 1970us pulse with 1936 us gap
 * 
 * Demoding with -X "tfa_test:OOK_PPM_RAW:2900:5000:36500"
 * 
 * 74 bit (2 bit preamble and 72 bit data => 9 bytes => 18 nibbles)
 * The preamble seems to be a repeat counter (00, and 01 seen),
 * the first 4 bytes are data,
 * the second 4 bytes the same data inverted,
 * the last byte is a checksum.
 * 
 * Data: HHHHhhhh ??CCNIII IIIITTTT ttttuuuu 
 *     H = First BCD digit humidity (the MSB might be distorted by the demod)
 *     h = Second BCD digit humidity
 *     ? = Likely battery flag
 *     C = Channel
 *     N = Negative temperature sign bit
 *     I = Unknown
 *     T = First BCD digit temperature
 *     t = Second BCD digit temperature
 *     u = Third BCD digit temperature
 * 
 * The Checksum seems to cover the data bytes and is roughly something like:
 *
 *  = (b[0] & 0x5) + (b[0] & 0xf) << 4  + (b[0] & 0x50) >> 4 + (b[0] & 0xf0)
 *  + (b[1] & 0x5) + (b[1] & 0xf) << 4  + (b[1] & 0x50) >> 4 + (b[1] & 0xf0)
 *  + (b[2] & 0x5) + (b[2] & 0xf) << 4  + (b[2] & 0x50) >> 4 + (b[2] & 0xf0)
 *  + (b[3] & 0x5) + (b[3] & 0xf) << 4  + (b[3] & 0x50) >> 4 + (b[3] & 0xf0)
 */

#include "decoder.h"

static int tfa_303211_callback(bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t b[9] = {0};

    /* length check */
    if (74 != bitbuffer->bits_per_row[0]) {
        if( debug_output)
            fprintf(stderr, "tfa_303211 wrong size (%i bits)\n", bitbuffer->bits_per_row[0]);
        return 0;
    }

	/* dropping 2 bit preamle */
    bitbuffer_extract_bytes(bitbuffer, 0, 2, b, 72);

    // flip inverted bytes
    b[4] ^= 0xff;
    b[5] ^= 0xff;
    b[6] ^= 0xff;
    b[7] ^= 0xff;

    // restore first MSB
    b[0] = (b[0] & 0x7f) | (b[4] & 0x80);

    // check bit-wise parity
    if (b[0] != b[4] || b[1] != b[5] || b[2] != b[6] || b[3] != b[7])
        return 0;

    float const temp        = ((b[2] & 0x0F) * 10) + ((b[3] & 0xF0) >> 4) + ((b[3] & 0x0F) *0.1F);
    int const minus         = (b[1] & 0x08) >> 3;
    int const hum           = (b[0] & 0x70) >> 4;
    int const humidity      = (hum < 2 ? hum + 8 : hum) * 10 + (b[0] & 0x0F);
    int const sensor_id     = ((b[1] & 0x07) << 4) | ((b[2] & 0xF0) >> 4);
    int const battery_low   = 0;
    int const channel       = (b[1] & 0x30) >> 4;

    float temp_c = (minus == 1 ? temp * -1 : temp);

    char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);

    data = data_make(
            "time",          "",            DATA_STRING, time_str,
            "model",         "",            DATA_STRING, "TFA 30.3211.02",
            "id",            "",            DATA_INT, sensor_id,
            "channel",       "",            DATA_INT, channel,
            //"battery",       "Battery",     DATA_STRING, "??",
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",           "MIC",         DATA_STRING, "CHECKSUM", // actually a per-bit parity, chksum unknown
            NULL);
    data_acquired_handler(data);

    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "channel",
    //"battery",
    "temperature_C",
    "humidity",
    "mic",
    NULL
};

r_device tfa_30_3211 = {
    .name          = "TFA 30.3211.02 Temperature/Humidity Sensor",
    .modulation    = OOK_PULSE_PPM_RAW,
    .short_limit   = 2900,
    .long_limit    = 6000,
    .reset_limit   = 36500,
    .json_callback = &tfa_303211_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = output_fields
};
