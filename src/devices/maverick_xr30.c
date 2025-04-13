/** @file
    Maverick XR-30 BBQ Sensor.

    Copyright (C) 2022 jbfunk
    Heavily derived from Maverick ET-73x BBQ Sensor (maverick_et73x.c), Copyright (C) 2016 gismo2004

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Maverick XR-30 BBQ Sensor.

The thermometer transmits 4 identical messages every 12 seconds at 433.92 MHz,

Each message consists of 26 nibbles (104 bits total) but the first (non-data) bit (1) is getting dropped sometimes in reception, so for analysis the payload is shifted 7 bits left to align the bytes (or 8 bits if 0xaa is observed rather than 0x55 as the first byte received)

Payload:

- P = 32 bit preamble (0xaaaaaaaa; 7 or 8 bits shifted left for analysis)
- S = 32 bit sync-word (0xd391d391)
- F =  4 bit device state (0=default; 5=init)
- T = 10 bit temp1 (degree C, offset by 532)
- t = 10 bit temp2 (degree C, offset by 532)
- D = 16 bit digest (over FTt, includes non-transmitted device id renewed on a device reset) gen 0x8810 init 0x0d42

    byte (after shift):       0   1   2   3   4   5     6     7     8     9     10    11
    nibble (after shift):     0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
    msg:                  P P P P P P P P S S S S S  S  S  S  F  T  T  Tt t  t  D  D  D  D
    PRE:32h SYNC: 32h FLAG:4h T:10d t:10d | DIGEST:16h

*/

#include "decoder.h"

static int maverick_xr30_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;

    if (bitbuffer->num_rows != 1)
        return DECODE_ABORT_EARLY;

    //check correct data length
    if (bitbuffer->bits_per_row[0] != 104) // 104 bits
        return DECODE_ABORT_LENGTH;

    uint8_t b[12];
    //check for correct preamble/sync (0xaaaaaad391d391)
    if (bitbuffer->bb[0][0] == 0x55) {
        bitbuffer_extract_bytes(bitbuffer, 0, 7, b, 12 * 8); // shift in case first bit was not received properly
    } else if (bitbuffer->bb[0][0] == 0xaa) {
        bitbuffer_extract_bytes(bitbuffer, 0, 8, b, 12 * 8); // shift in case first bit was received properly
    } else {
        return DECODE_ABORT_EARLY; // preamble/sync missing
    }
    if (b[0] != 0xaa || b[1] != 0xaa || b[2] != 0xaa || b[3] != 0xd3 || b[4] != 0x91 || b[5] != 0xd3 || b[6] != 0x91)
        return DECODE_ABORT_EARLY; // preamble/sync missing

    int sync   = b[3] << 24 | b[4] << 16 | b[5] << 8 | b[6];
    int flags  = (b[7] & 0xf0) >> 4;
    int temp1  = (b[7] & 0x0f) << 6 | (b[8] & 0xfc) >> 2;
    int temp2  = (b[8] & 0x03) << 8 | b[9];
    int digest = b[10] << 8 | b[11];

    float temp1_c = temp1 - 532.0f;
    float temp2_c = temp2 - 532.0f;

    char const *status = "unknown";
    if (flags == 0)
        status = "default";
    else if (flags == 5)
        status = "init";

    //digest is used to represent a session. This means, we get a new id if a reset or battery exchange is done.
    int id = lfsr_digest16(&b[7], 3, 0x8810, 0x0d42) ^ digest;

    decoder_logf(decoder, 1, __func__, "sync %08x, flags %x, t1 %d, t2 %d, digest %04x, chk_data %02x%02x%02x, digest xor'ed: %04x",
                sync, flags, temp1, temp2, digest, b[7], b[8], b[9], id);

    /* clang-format off */
    data = data_make(
            "model",            "",                     DATA_STRING, "Maverick-XR30",
            "id",               "Session_ID",           DATA_INT,    id,
            "status",           "Status",               DATA_STRING, status,
            "temperature_1_C",  "TemperatureSensor1",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp1_c,
            "temperature_2_C",  "TemperatureSensor2",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp2_c,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "status",
        "temperature_1_C",
        "temperature_2_C",
        "mic",
        NULL,
};

r_device const maverick_xr30 = {
        .name        = "Maverick XR-30 BBQ Sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 360,
        .long_width  = 360,
        .reset_limit = 4096,
        .decode_fn   = &maverick_xr30_callback,
        .fields      = output_fields,
};
