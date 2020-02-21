/** @file
    inFactory outdoor temperature and humidity sensor.
    Other brand names: nor-tec, GreenBlue

    Copyright (C) 2017 Sirius Weiß <siriuz@gmx.net>
    Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>
    Copyright (C) 2020 Hagen Patzke <hpatzke@gmx.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
inFactory Outdoor sensor transmits data temperature, humidity,
          NC-3982-913 from Pearl (for FWS-686 station)

Transmissions also includes an id. Every 60 seconds
the sensor transmits 6 packets:

    0000 1111 | 0011 0000 | 0101 1100 | 1110 0111 | 0110 0001
    iiii iiii | cccc pw?? | tttt tttt | tttt hhhh | hhhh ??nn

- i: identification // changes on battery switch
- c: CRC-4 // CCITT checksum, see below for computation specifics
- p: power-on // flag to indicate power-on (e.g. battery change)
- w: weak-battery // flag to indicate low battery voltage
- h: Humidity // BCD-encoded, each nibble is one digit, 'A0' means 100%rH
- t: Temperature // in °F as binary number with one decimal place + 90 °F offset
- n: Channel // Channel number 1 - 3

Usage:

    # rtl_433 -f 434052000 -R 91 -F json:log.json

Payload looks like this:

    [00] { 4} 00             : 0000
    [01] {40} 0f 30 5c e7 61 : 00001111 00110000 01011100 11100111 01100001

First row is actually the preamble part. This makes the signal more unique
but also trains the receiver AGC (automatic gain control).
*/

#include "decoder.h"

static void infactory_raw_msg(char *msg, uint8_t *b) {
    int i;
    char c;
    char *p = msg;
    for (i = 0; i < 10; i++) {
        c = ((b[i >> 1]) >> (4 * (1 - (i & 1)))) & 0x0F;
        c = (c > 9) ? c + 55 : c + 48;
        *p++ = c;
    }
    *p = 0;
}

static char* infactory_crc_check(uint8_t *b) {
    int i;
    uint8_t msg_crc, crc;
    uint8_t msg[5];
    memcpy(msg, b, 5);
    msg_crc = msg[1] >> 4;
    // for CRC computation, channel bits are at CRC position(!)
    msg[1] = (msg[1] & 0x0F) | (msg[4] & 0x0F) << 4;
    crc = msg[0] >> 4;
    for ( i = 4; i < 36; i++) {
        crc <<= 1;
        if (msg[i>>3] & (0x80 >> (i&7))) {
            crc |= 1; 
        }
        if (crc & 0x10) {
            crc ^= 0x13; // Koopmann 0x9, CCITT-4; FP-4; ITU-T G.704
        }
    }

    return (crc == msg_crc) ? "CRC_OK" : "CRC_ERROR";
}

static int infactory_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitrow_t *bb;
    data_t *data;
    uint8_t *b;
    int id, humidity, temp_raw, channel, pwron, weakbatt;
    float temp_f;
    char *mic = "ERROR";
    char raw_msg[11] = ".........."; // 10 hex chars plus 0

    if (bitbuffer->bits_per_row[0] != 4) {
        return DECODE_ABORT_LENGTH;
    }
    if (bitbuffer->bits_per_row[1] != 40) {
        return DECODE_ABORT_LENGTH;
    }
    bb = bitbuffer->bb;

    /* Check that 4 preamble bits are 0 */
    b = bb[0];
    if (b[0]&0x0F)
        return DECODE_ABORT_EARLY;

    /* Check that the last 4 bits of message are not 0 (channel number) */
    b = bb[1];
    if (!(b[4]&0x0F))
        return DECODE_ABORT_EARLY;

    id       = b[0];
    pwron    = (b[1] >> 3) & 1;
    weakbatt = (b[1] >> 2) & 1;
    humidity = (b[3] & 0x0F) * 10 + (b[4] >> 4); // BCD, 'A0'=100%rH
    temp_raw = (b[2] << 4) | (b[3] >> 4);
    temp_f   = (float)temp_raw * 0.1 - 90;
    channel  = b[4] & 0x03;

    mic = infactory_crc_check(b);
    infactory_raw_msg(raw_msg, b);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, _X("inFactory-TH","inFactory sensor"),
            "id",               "ID",           DATA_INT, id,
            "channel",          "Channel",      DATA_INT, channel,
            "weakbatt",         "Battery",      DATA_INT, weakbatt,
            "temperature_F",    "Temperature",  DATA_FORMAT, "%.02f °F", DATA_DOUBLE, temp_f,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",              "Integrity",    DATA_STRING, mic,
            "poweron",          "Power-On",     DATA_INT, pwron,
            "rawmsg",           "Datagram",     DATA_STRING, raw_msg,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "temperature_F",
        "humidity",
        "mic",
        "poweron",
        "weakbatt",
        "rawmsg",
        NULL,
};

r_device infactory = {
        .name        = "inFactory, nor-tec, FreeTec NC-3982-913 temperature humidity sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1850,
        .long_width  = 4050,
        .gap_limit   = 4000, // Maximum gap size before new row of bits [us]
        .reset_limit = 8500, // Maximum gap size before End Of Message [us].
        .tolerance   = 1000,
        .decode_fn   = &infactory_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
