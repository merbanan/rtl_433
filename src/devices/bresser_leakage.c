/** @file
    Bresser Water Leakage Sensor.

    Copyright (C) 2023 Matthias Prinke <m.prinke@arcor.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define SENSOR_TYPE_LEAKAGE 5

/**
Bresser Water Leakage Sensor.

Decoder for Bresser Water Leakage outdoor sensor, PN 7009975

see https://github.com/merbanan/rtl_433/issues/2576

Based on bresser_6in1.c

Preamble: aa aa 2d d4

Data layout:

    CCCCCCCC CCCCCCCC IIIIIIII IIIIIIII IIIIIIII IIIIIIII SSSSQHHH ANBBFFFF

- C: 16-bit, crc16/xmodem, polynomial: 0x1021, init: 0x0000, range: byte 2...6
- I: 24-bit little-endian id; changes on power-up/reset
- S: 4 bit sensor type
- Q: 1 bit startup; changes from 0 to 1 approx. one hour after power-on/reset
- H: 3 bit channel; set via switch on the device, latched at power-on/reset
- A: 1 bit alarm
- N: 1 bit no_alarm; inverse of alarm
- B: 2 bit battery state; 0b11 if battery is o.k.
- F: 4 bit flags (always 0b0000)


Examples:

    [Bresser Water Leakage Sensor, PN 7009975]

    [00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25]

     C7 70 35 97 04 08 57 70 00 00 00 00 00 00 00 00 03 FF FF FF FF FF FF FF FF FF [CH7]
     DF 7D 36 49 27 09 56 70 00 00 00 00 00 00 00 00 03 FF FF FF FF FF FF FF FF FF [CH6]
     9E 30 79 84 33 06 55 70 00 00 00 00 00 00 00 00 03 FF FD DF FF BF FF DF FF FF [CH5]
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

static int bresser_leakage_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0x2d, 0xd4};
    uint8_t msg[18];

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

    // CRC check
    uint16_t crc_calculated = crc16(&msg[2], 5, 0x1021, 0x0000);
    uint16_t crc_received = msg[0] << 8 | msg[1];
    decoder_logf(decoder, 2, __func__, "CRC 0x%04X = 0x%04X", crc_calculated, crc_received);
    if (crc_received != crc_calculated) {
        decoder_logf(decoder, 1, __func__, "CRC check failed (0x%04X != 0x%04X)", crc_calculated, crc_received);
        return DECODE_FAIL_MIC;
    }

    uint32_t sensor_id = ((uint32_t)msg[2] << 24) | (msg[3] << 16) | (msg[4] << 8) | (msg[5]);
    int s_type         = msg[6] >> 4;
    int chan           = (msg[6] & 0x7);
    int battery_ok     = ((msg[7] & 0x30) != 0x00);
    int nstartup       = (msg[6] & 0x08) >> 3;
    int alarm          = (msg[7] & 0x80) >> 7;
    int no_alarm       = (msg[7] & 0x40) >> 6;

    // Sanity checks
    if (s_type != SENSOR_TYPE_LEAKAGE
            || alarm == no_alarm
            || chan == 0) {
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Bresser-Leakage",
            "id",               "",             DATA_FORMAT, "%08x",   DATA_INT,    sensor_id,
            "channel",          "",             DATA_INT,    chan,
            "battery_ok",       "Battery",      DATA_INT,    battery_ok,
            "alarm",            "Alarm",        DATA_INT,    alarm,
            "startup",          "Startup",      DATA_COND,   !nstartup,  DATA_INT,  !nstartup,
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
        "alarm",
        "startup",
        NULL,
};

r_device const bresser_leakage = {
        .name        = "Bresser water leakage",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 124,
        .long_width  = 124,
        .reset_limit = 25000,
        .decode_fn   = &bresser_leakage_decode,
        .fields      = output_fields,
};
