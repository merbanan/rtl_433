/** @file
    Bresser Water Leakage Sensor.

    Copyright (C) 2023 Matthias Prinke <m.prinke@arcor.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#define SENSOR_TYPE_LEAKAGE 5

/**
Decoder for Bresser Water Leakage outdoor sensor, PN 7009975

see https://github.com/merbanan/rtl_433/issues/2576

Based on bresser_6in1.c
 
Preamble: aa aa 2d d4

Data layout:
    (preferably use one character per bit)
    CCCCCCCC CCCCCCCC IIIIIIII IIIIIIII IIIIIIII IIIIIIII SSSSQHHH ANBBFFFF
    
- C: 16-bit, presumably checksum, algorithm unknown
- I: 24-bit little-endian id; changes on power-up/reset
- S: 4 bit sensor type
- Q: 1 bit startup; changes from 0 to 1 approx. one hour after power-on/reset
- H: 3 bit channel; set via switch on the device, latched at power-on/reset
- A: 1 bit alarm
- N: 1 bit no_alarm; inverse of alarm
- B: 2 bit battery state; 0b11 if battery is o.k.
- F: 4 bit flags (always 0b0000)


Examples:
---------
[Bresser Water Leakage Sensor, PN 7009975]
 
[00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25]
  
 C7 70 35 97 04 08 57 70 00 00 00 00 00 00 00 00 03 FF FF FF FF FF FF FF FF FF [CH7]
 DF 7D 36 49 27 09 56 70 00 00 00 00 00 00 00 00 03 FF FF FF FF FF FF FF FF FF [CH6]
 9E 30 79 84 33 06 55 70 00 00 00 00 00 00 00 00 03 FF FD DF FF BF FF DF FF FF [CH5]
 37 D8 57 19 73 02 51 70 00 00 00 00 00 00 00 00 03 FF FF FF FF FF BF FF EF FB [set CH4, received CH1 -> switch not positioned correctly]
 E2 C8 68 27 91 24 54 70 00 00 00 00 00 00 00 00 03 FF FF FF FF FF FF FF FF FF [CH4]
 B3 DA 55 57 17 40 53 70 00 00 00 00 00 00 00 00 03 FF FF FF FF FF FF FF FF FB [CH3]
 37 FA 84 73 03 02 52 70 00 00 00 00 00 00 00 00 03 FF FF FF DF FF FF FF FF FF [CH2]
 27 F3 80 02 52 88 51 70 00 00 00 00 00 00 00 00 03 FF FF FF FF FF DF FF FF FF [CH1]
 A6 FB 80 02 52 88 59 70 00 00 00 00 00 00 00 00 03 FD F7 FF FF BF FF FF FF FF [CH1+NSTARTUP]
 A6 FB 80 02 52 88 59 B0 00 00 00 00 00 00 00 00 03 FF FF FF FD FF F7 FF FF FF [CH1+NSTARTUP+ALARM]
 A6 FB 80 02 52 88 59 70 00 00 00 00 00 00 00 00 03 FF FF BF F7 F7 FD 7F FF FF [CH1+NSTARTUP]
 [Reset]
 C0 10 36 79 37 09 51 70 00 00 00 00 00 00 00 00 01 1E FD FD FF FF FF DF FF FF [CH1]
 C0 10 36 79 37 09 51 B0 00 00 00 00 00 00 00 00 03 FE FD FF AF FF FF FF FF FD [CH1+ALARM]
 [Reset]
 71 9C 54 81 72 09 51 40 00 00 00 00 00 00 00 00 0F FF FF FF FF FF FF DF FF FE [CH1+BATT_LO]
 71 9C 54 81 72 09 51 40 00 00 00 00 00 00 00 00 0F FE FF FF FF FF FB FF FF FF
 71 9C 54 81 72 09 51 40 00 00 00 00 00 00 00 00 07 FD F7 FF DF FF FF DF FF FF
 71 9C 54 81 72 09 51 80 00 00 00 00 00 00 00 00 1F FF FF F7 FF FF FF FF FF FF [CH1+BATT_LO+ALARM]
 F0 94 54 81 72 09 59 40 00 00 00 00 00 00 00 00 0F FF DF FF FF FF FF BF FD F7 [CH1+BATT_LO+NSTARTUP]
 F0 94 54 81 72 09 59 80 00 00 00 00 00 00 00 00 03 FF B7 FF ED FF FF FF DF FF [CH1+BATT_LO+NSTARTUP+ALARM]

 */

#include "decoder.h"

static int bresser_leakage(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0x2d, 0xd4};
    data_t *data;
    uint8_t msg[18];
    uint32_t sensor_id;
    uint8_t s_type;
    uint8_t chan;
    uint8_t startup;
    uint8_t battery_ok;
    bool alarm;
    bool no_alarm
    int leakage_alarm;
    bool decode_ok;
    uint8_t null_bytes;
  
    if (bitbuffer->num_rows != 1
            || bitbuffer->bits_per_row[0] < 160
            || bitbuffer->bits_per_row[0] > 440) {
        decoder_logf(decoder, 2, __func__, "bit_per_row %u out of range", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_EARLY; // Unrecognized data
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof (preamble_pattern) * 8);

    if (start_pos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }
    start_pos += sizeof (preamble_pattern) * 8;

    unsigned len = bitbuffer->bits_per_row[0] - start_pos;
    if (len < sizeof(msg) * 8) {
        decoder_logf(decoder, 2, __func__, "%u too short", len);
        return DECODE_ABORT_LENGTH; // message too short
    }

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, sizeof(msg) * 8);

    decoder_log_bitrow(decoder, 2, __func__, msg, sizeof(msg) * 8, "");
    
    // TODO: Find parity/checksum/checksum algorithm

    sensor_id     = ((uint32_t)msg[2] << 24) | (msg[3] << 16) | (msg[4] << 8) | (msg[5]);
    s_type        = msg[6] >> 4;
    chan          = (msg[6] & 0x7);
    battery_ok    = ((msg[7] & 0x30) != 0x00) ? 1 : 0;
    startup       = (msg[6] >> 3) & 1;
    alarm         = (msg[7] & 0x80) == 0x80;
    no_alarm      = (msg[7] & 0x40) == 0x40;
    leakage_alarm = (alarm) ? 1 : 0;
    
    null_bytes = msg[7] & 0xF;

    for (int i=8; i<=15; i++) {
        null_bytes |= msg[i];
    }

    // The parity/checksum/digest algorithm is currently unknown.
    // We apply some heuristics to validate that the message is really from
    // a water leakage sensor.
    decode_ok = (s_type == SENSOR_TYPE_LEAKAGE) &&
                (alarm != no_alarm) && 
                (chan != 0) && 
                (null_bytes == 0);
   
   if (!decode_ok)
       return 0;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Bresser-Leakage",
            "id",               "",             DATA_FORMAT, "%08x",   DATA_INT,    sensor_id,
            "channel",          "",             DATA_INT,    chan,
            "battery_ok",       "Battery",      DATA_INT,    battery_ok,
            "alarm",            "Alarm",        DATA_INT,    leakage_alarm,
            "startup",          "Startup",      DATA_COND,   startup,  DATA_INT,    startup,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
    "model",
    "id",
    "channel",
    "battery_ok",
    "startup",
    "alarm",
    NULL,
};

r_device const bresser_6in1 = {
    .name        = "Bresser water leakage",
    .modulation  = FSK_PULSE_PCM,
    .short_width = 124,
    .long_width  = 124,
    .reset_limit = 25000,
    .decode_fn   = &bresser_leakage_decode,
    .fields      = output_fields,
};
