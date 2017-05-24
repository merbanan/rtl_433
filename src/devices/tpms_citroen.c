/* Citroen FSK 10 byte Manchester encoded checksummed TPMS data
 * also Peugeot and likely Fiat, Mitsubishi, VDO-types.
 *
 * Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Packet nibbles:  UU  IIIIIIII FR  PP TT BB  CC
 * U = state, decoding unknown, not included in checksum
 * I = id
 * F = flags, (seen: 0: 69.4% 1: 0.8% 6: 0.4% 8: 1.1% b: 1.9% c: 25.8% e: 0.8%)
 * R = repeat counter (seen: 0,1,2,3)
 * P = Pressure (maybe bar in 0.0125 steps, or offset /differential)
 * T = Temperature (looks like deg C offset by 50)
 * B = Battery?
 * C = Checksum, XOR bytes 1 to 9 = 0
 */

#include "rtl_433.h"
#include "util.h"

// full preamble is
// 0101 0101  0101 0101  0101 0101  0101 0110 = 55 55 55 56
static const unsigned char preamble_pattern[2] = { 0x55, 0x56 };
// full trailer is 01111110

static int tpms_citroen_decode(bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos) {
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    unsigned int start_pos;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    int state;
    char state_str[3];
    unsigned id;
    char id_str[9];
    int flags;
    int repeat;
    int pressure;
    int temperature;
    int battery;
    char code_str[7];
    int crc;

    bitbuffer_invert(bitbuffer);
    start_pos = bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 88);
    b = packet_bits.bb[0];

    if (b[6] == 0 || b[7] == 0) {
        return 0; // sanity check failed
    }

    crc = b[1]^b[2]^b[3]^b[4]^b[5]^b[6]^b[7]^b[8]^b[9];
    if (crc != 0) {
        return 0; // bad checksum
    }

    state = b[0]; // not covered by CRC
    sprintf(state_str, "%02x", state);
    id = b[1]<<24 | b[2]<<16 | b[3]<<8 | b[4];
    sprintf(id_str, "%08x", id);
    flags = b[5]>>4;
    repeat = b[5]&0x0f;
    pressure = b[6];
    temperature = b[7];
    battery = b[8];
    sprintf(code_str, "%02x%02x%02x", pressure, temperature, battery);

    local_time_str(0, time_str);
    data = data_make(
        "time",         "",     DATA_STRING, time_str,
        "model",        "",     DATA_STRING, "Citroen",
        "type",         "",     DATA_STRING, "TPMS",
        "state",        "",     DATA_STRING, state_str,
        "id",           "",     DATA_STRING, id_str,
        "flags",        "",     DATA_INT, flags,
        "repeat",       "",     DATA_INT, repeat,
//        "pressure_bar", "Pressure",    DATA_FORMAT, "%.03f bar", DATA_DOUBLE, (double)pressure*0.0125,
//        "temperature_C", "Temperature", DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature-50.0,
//        "battery_mV",   "Battery", DATA_INT, battery_mV,
        "code",         "",     DATA_STRING, code_str,
        "mic",          "",     DATA_STRING, "CHECKSUM",
        NULL);

    data_acquired_handler(data);
    return 1;
}

static int tpms_citroen_callback(bitbuffer_t *bitbuffer) {
    unsigned bitpos = 0;
    int events = 0;

    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, (uint8_t *)&preamble_pattern, 16)) + 178 <=
            bitbuffer->bits_per_row[0]) {
        events += tpms_citroen_decode(bitbuffer, 0, bitpos + 16);
        bitpos += 2;
    }

    return events;
}

static char *output_fields[] = {
    "time",
    "model",
    "type",
    "state",
    "id",
    "flags",
    "repeat",
//    "pressure_bar",
//    "temperature_C",
//    "battery_mV",
    "code",
    "mic",
    NULL
};

r_device tpms_citroen = {
    .name           = "Citroen TPMS",
    .modulation     = FSK_PULSE_PCM,
    .short_limit    = 52, // 12-13 samples @250k
    .long_limit     = 52, // FSK
    .reset_limit    = 150, // Maximum gap size before End Of Message [us].
    .json_callback  = &tpms_citroen_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields,
};
