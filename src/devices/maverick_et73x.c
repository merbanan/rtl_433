/** @file
    Maverick ET-73x BBQ Sensor.

    Copyright (C) 2016 gismo2004
    Credits to all users of mentioned forum below!

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Maverick ET-73x BBQ Sensor.

FCC-Id: TKCET-733

The thermometer transmits 4 identical messages every 12 seconds at 433.92 MHz,
using on-off keying and 2000bps Manchester encoding,
with each message preceded by 8 carrier pulses 230 us wide and 5 ms apart.

Each message consists of 26 nibbles (104 bits total) which are again manchester (IEEE) encoded (52 bits)
For nibble 24 some devices are sending 0x1 or 0x2 ?

Payload:

- P = 12 bit Preamble (raw 0x55666a, decoded 0xfa8)
- F =  4 bit device state (2=default; 7=init)
- T = 10 bit temp1 (degree C, offset by 532)
- t = 10 bit temp2 (degree C, offset by 532)
- D = 16 bit digest (over FTt, includes non-transmitted device id renewed on a device reset) gen 0x8810 init 0xdd38

    nibble: 0 1 2 3 4 5 6  7 8 9 10 11 12
    msg:    P P P F T T Tt t t D D  D  D
    PRE:12h FLAG:4h TA:10d TB:10d | DIGEST:16h

further information can be found here: https://forums.adafruit.com/viewtopic.php?f=8&t=25414
note that the mentioned quaternary conversion is actually manchester code.
*/

#include "decoder.h"

static int maverick_et73x_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    bitbuffer_t mc = {0};

    if (bitbuffer->num_rows != 1)
        return DECODE_ABORT_EARLY;

    //check correct data length
    if (bitbuffer->bits_per_row[0] != 104) // 104 raw half-bits, 52 bits payload
        return DECODE_ABORT_LENGTH;

    //check for correct preamble (0x55666a)
    if ((bitbuffer->bb[0][0] != 0x55) || bitbuffer->bb[0][1] != 0x66 || bitbuffer->bb[0][2] != 0x6a)
        return DECODE_ABORT_EARLY; // preamble missing

    // decode the inner manchester encoding
    bitbuffer_manchester_decode(bitbuffer, 0, 0, &mc, 104);

    // we require 7 bytes 13 nibble rounded up (b[6] highest reference below)
    if (mc.bits_per_row[0] < 52) {
        return DECODE_FAIL_SANITY; // manchester_decode fail
    }

    uint8_t *b = mc.bb[0];
    int pre    = (b[0] << 4) | (b[1] & 0xf0) >> 4;
    int flags  = b[1] & 0x0f;
    int temp1  = (b[2] << 2) | (b[3] & 0xc0) >> 6;
    int temp2  = (b[3] & 0x3f) << 4 | (b[4] & 0xf0) >> 4;
    int digest = (b[4] & 0x0f) << 12 | b[5] << 4 | b[6] >> 4;

    float temp1_c = temp1 - 532.0f;
    float temp2_c = temp2 - 532.0f;

    char const *status = "unknown";
    if (flags == 2)
        status = "default";
    else if (flags == 7)
        status = "init";

    uint8_t chk[3];
    bitbuffer_extract_bytes(&mc, 0, 12, chk, 24);

    //digest is used to represent a session. This means, we get a new id if a reset or battery exchange is done.
    int id = lfsr_digest16(chk, 3, 0x8810, 0xdd38) ^ digest;

    decoder_logf(decoder, 1, __func__, "pre %03x, flags %0x, t1 %d, t2 %d, digest %04x, chk_data %02x%02x%02x, digest xor'ed: %04x",
                pre, flags, temp1, temp2, digest, chk[0], chk[1], chk[2], id);

    /* clang-format off */
    data = data_make(
            "model",            "",                     DATA_STRING, "Maverick-ET73x",
            "id",               "Session_ID",           DATA_INT,    id,
            "status",           "Status",               DATA_STRING, status,
            "temperature_1_C",  "TemperatureSensor1",   DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp1_c,
            "temperature_2_C",  "TemperatureSensor2",   DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp2_c,
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

r_device const maverick_et73x = {
        .name        = "Maverick ET-732/733 BBQ Sensor",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 230,
        .long_width  = 0, //not used
        .reset_limit = 4000,
        //.reset_limit = 6000, // if pulse_slicer_manchester_zerobit implements gap_limit
        //.gap_limit   = 1000, // if pulse_slicer_manchester_zerobit implements gap_limit
        .decode_fn   = &maverick_et73x_callback,
        .fields      = output_fields,
};
