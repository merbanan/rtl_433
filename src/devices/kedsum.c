/** @file
    Kedsum temperature and humidity sensor (http://amzn.to/25IXeng).
    My models transmit at a bit lower freq. of around 433.71 Mhz.
    Also NC-7415 from Pearl.

    Copyright (C) 2016 John Lifsey
    Enhanced (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
*/
/**
Largely the same as esperanza_ews, s3318p.
\sa esperanza_ews.c s3318p.c

Frame structure:

    Byte:      0        1        2        3        4
    Nibble:    1   2    3   4    5   6    7   8    9   10
    Type:   00 IIIIIIII BBCC++++ ttttTTTT hhhhHHHH FFFFXXXX

- I: unique id. changes on powercycle
- B: Battery state 10 = Ok, 01 = weak, 00 = bad
- C: channel, 00 = ch1, 10=ch3
- + low temp nibble
- t: med temp nibble
- T: high temp nibble
- h: humidity low nibble
- H: humidity high nibble
- F: flags
- X: CRC-4 poly 0x3 init 0x0 xor last 4 bits
*/

#include "decoder.h"

static int kedsum_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    uint8_t b[5];
    data_t *data;

    // the signal should start with 15 sync pulses (empty rows)
    // require at least 5 received syncs
    if (bitbuffer->num_rows < 5
            || bitbuffer->bits_per_row[0] != 0
            || bitbuffer->bits_per_row[1] != 0
            || bitbuffer->bits_per_row[2] != 0
            || bitbuffer->bits_per_row[3] != 0
            || bitbuffer->bits_per_row[4] != 0)
        return 0;

    // the signal should have 6 repeats with a sync pulse between
    // require at least 4 received repeats
    int r = bitbuffer_find_repeated_row(bitbuffer, 4, 42);
    if (r < 0 || bitbuffer->bits_per_row[r] != 42)
        return 0;

    // remove the two leading 0-bits and align the data
    bitbuffer_extract_bytes(bitbuffer, r, 2, b, 40);

    // CRC-4 poly 0x3, init 0x0 over 32 bits then XOR the next 4 bits
    int crc = crc4(b, 4, 0x3, 0x0) ^ (b[4] >> 4);
    if (crc != (b[4] & 0xf))
        return 0;

    int id       = b[0];
    int battery  = b[1] >> 6;
    int channel  = ((b[1] & 0x30) >> 4) + 1;
    int temp_raw = ((b[2] & 0x0f) << 8) | (b[2] & 0xf0) | (b[1] & 0x0f);
    int humidity = ((b[3] & 0x0f) << 4) | ((b[3] & 0xf0) >> 4);
    float temp_f = (temp_raw - 900) * 0.1;

    char *battery_str = battery == 2 ? "OK" : battery == 1 ? "WEAK" : "LOW";

    int flags = (b[1] & 0xc0) | (b[4] >> 4);

    data = data_make(
            "model",            "",             DATA_STRING, _X("Kedsum-TH","Kedsum Temperature & Humidity Sensor"),
            "id",               "ID",           DATA_INT, id,
            "channel",          "Channel",      DATA_INT, channel,
            "battery",          "Battery",      DATA_STRING, battery_str,
            "flags",            "Flags2",       DATA_INT, flags,
            "temperature_F",    "Temperature",  DATA_FORMAT, "%.02f F", DATA_DOUBLE, temp_f,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "battery",
    "flags",
    "temperature_F",
    "humidity",
    "mic",
    NULL
};

r_device kedsum = {
    .name           = "Kedsum Temperature & Humidity Sensor, Pearl NC-7415",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 2000,
    .long_width     = 4000,
    .gap_limit      = 4400,
    .reset_limit    = 9400,
    .decode_fn      = &kedsum_callback,
    .disabled       = 0,
    .fields         = output_fields
};
