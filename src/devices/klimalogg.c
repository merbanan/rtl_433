/** @file
    Klimalogg/30.3180.IT sensor decoder

    Copyright (C) 2020 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**

Random information:

Working decoder
https://github.com/baycom/tfrec

Information from https://github.com/baycom/tfrec
// Telegram format
// 0x2d 0xd4 ID ID sT TT HH BB SS 0x56 CC
// 2d d4: Sync bytes
// ID(14:0): 15 bit ID of sensor (printed on the back and displayed after powerup)
// ID(15) is either 1 or 0 (fixed, depends on the sensor)
// s(3:0): Learning sequence 0...f, after learning fixed 8
// TTT: Temperatur in BCD in .1degC steps, offset +40degC (-> -40...+60)
// HH(6:0): rel. Humidity in % (binary coded, no BCD!)
// BB(7): Low battery if =1
// BB(6:4): 110 or 111 (for 3199)
// SS(7:4): sequence number (0...f)
// SS(3:0): 0000 (fixed)
// 56: Type?
// CC: CRC8 from ID to 0x56 (polynome x^8 + x^5 + x^4  + 1)


Note: The rtl_433 generic dsp code does not work well with these signals
play with the -l option (5000-15000 range) or a high sample rate.

*/

static uint8_t bcd_decode8(uint8_t x)
{
    return ((x & 0xF0) >> 4) * 10 + (x & 0x0F);
}


static int klimalogg_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    static const uint8_t KLIMA_PREAMBLE[]  = {0xB4, 0x2B };  // 0x2d, 0xd4 bit reversed
    unsigned int bit_offset;
    uint8_t *b;
    uint8_t msg[12] = {0};
    uint8_t crc, sequence_nr, temp_ad, humidity, battery_low;
    int16_t id, temp_bd;
    char temperature_str[10] = {0};
    data_t *data;

    if (bitbuffer->bits_per_row[0] < 12*8) {
        return DECODE_ABORT_LENGTH;
    }

    bit_offset = bitbuffer_search(bitbuffer, 0, 0, KLIMA_PREAMBLE, sizeof(KLIMA_PREAMBLE)*8);
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, msg, 12*8);

    if (msg[9] != 0x6a) // 0x56 bit reversed
        return DECODE_FAIL_SANITY;


    for (int i=0 ; i<11 ; i++) {
        msg[i] = reverse8(msg[i]);
    }

    crc = crc8(&msg[2], 8, 0x31, 0);
    if (crc != msg[10])
        return DECODE_FAIL_MIC;

    /* Extract parameters */
    id = (msg[2]&0x7f)<<8 | msg[3];
    temp_bd = bcd_decode8((msg[4]&0x0F)<<4 | (msg[5]&0xF0)>>4) - 40;
    temp_ad = bcd_decode8((msg[5]&0x0F));
    sprintf(temperature_str, "%d.%d C", temp_bd, temp_ad);
    humidity = msg[6]&0x7F;
    battery_low = (msg[7]&0x80) >> 7;
    sequence_nr = (msg[8]&0xF0) >> 4;
    /* clang-format off */
    data = data_make(
            "model",           "",                 DATA_STRING, "Klimalogg Pro",
            "id",              "Id",               DATA_FORMAT,    "%04x", DATA_INT, id,
            "battery",          "Battery",         DATA_STRING, battery_low ? "LOW" : "OK",
            "temperature_C", "Temperature",        DATA_STRING, temperature_str,
            "humidity",        "Humidity",         DATA_INT, humidity,
            "sequence_nr","Sequence Number",       DATA_INT, sequence_nr,
            "mic",             "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 0;
}

static char *output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "battery",
        "humidity",
        "sequence_nr",
        "mic",
        NULL,
};

r_device klimalogg = {
        .name        = "Klimalogg",
        .modulation  = OOK_PULSE_NRZS,
        .short_width = 26,
        .long_width  = 0,
        .gap_limit   = 0,
        .reset_limit = 1000,
        .tolerance   = 0,
        .decode_fn   = &klimalogg_decode,
        .disabled    = 1,
        .fields      = output_fields,
};
