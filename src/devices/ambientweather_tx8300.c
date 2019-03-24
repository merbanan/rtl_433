/* Ambient Weather TX-8300 (also sold as TFA 30.3211.02)
 * Copyright (C) 2018 ionum-projekte and Roger
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
 * 74 bit (2 bit preamble and 72 bit data => 9 bytes => 18 nibbles)
 * The preamble seems to be a repeat counter (00, and 01 seen),
 * the first 4 bytes are data,
 * the second 4 bytes the same data inverted,
 * the last byte is a checksum.
 *
 * Preamble format (2 bits):
 *  [1 bit (0)] [1 bit rolling count]
 *
 * Payload format (32 bits):
 *   HHHHhhhh ??CCNIII IIIITTTT ttttuuuu
 *     H = First BCD digit humidity (the MSB might be distorted by the demod)
 *     h = Second BCD digit humidity, invalid humidity seems to be 0x0e
 *     ? = Likely battery flag, 2 bits
 *     C = Channel, 2 bits
 *     N = Negative temperature sign bit
 *     I = ID, 7-bit
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

static int ambientweather_tx8300_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t b[9] = {0};

    /* length check */
    if (74 != bitbuffer->bits_per_row[0]) {
        if (decoder->verbose > 1)
            fprintf(stderr, "AmbientWeather-TX8300: wrong size (%i bits)\n", bitbuffer->bits_per_row[0]);
        return 0;
    }

    /* dropping 2 bit preamble */
    bitbuffer_extract_bytes(bitbuffer, 0, 2, b, 72);

    // flip inverted bytes
    b[4] ^= 0xff;
    b[5] ^= 0xff;
    b[6] ^= 0xff;
    b[7] ^= 0xff;

    // restore first MSB
    b[0] = (b[0] & 0x7f) | (b[4] & 0x80);

    if (decoder->verbose > 1)
        fprintf(stderr, "H: %02x, F:%02x\n", b[0], b[1] & 0xc0);

    // check bit-wise parity
    if (b[0] != b[4] || b[1] != b[5] || b[2] != b[6] || b[3] != b[7])
        return 0;

    float temp        = (b[2] & 0x0f) * 10 + ((b[3] & 0xf0) >> 4) + (b[3] & 0x0f) * 0.1F;
    int channel       = (b[1] & 0x30) >> 4;
    int battery_low   = (b[1] & 0xc0) >> 6; // bit mapping unknown
    int minus         = (b[1] & 0x08) >> 3;
    int humidity      = ((b[0] & 0xf0) >> 4) * 10 + (b[0] & 0x0f);
    int sensor_id     = ((b[1] & 0x07) << 4) | ((b[2] & 0xf0) >> 4);
    float temp_c = (minus == 1 ? temp * -1 : temp);
    if (((b[0] & 0xf0) >> 4) > 9 || (b[0] & 0x0f) > 9) // invalid humidity
        humidity = -1;

    data = data_make(
            "model",         "",            DATA_STRING, "AmbientWeather-TX8300",
            "id",            "",            DATA_INT, sensor_id,
            "channel",       "",            DATA_INT, channel,
            "battery",       "Battery",     DATA_INT, battery_low, // mapping unknown
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            NULL);

    if (humidity >= 0)
        data = data_append(data,
                "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                NULL);

    data = data_append(data,
            "mic",           "MIC",         DATA_STRING, "CHECKSUM", // actually a per-bit parity, chksum unknown
            NULL);
    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "battery",
    "temperature_C",
    "humidity",
    "mic",
    NULL
};

r_device ambientweather_tx8300 = {
    .name          = "Ambient Weather TX-8300 Temperature/Humidity Sensor",
    .modulation    = OOK_PULSE_PPM,
    .short_width   = 2000,
    .long_width    = 4000,
    .gap_limit     = 6500,
    .reset_limit   = 8000,
    .decode_fn     = &ambientweather_tx8300_callback,
    .disabled      = 0,
    .fields        = output_fields
};
