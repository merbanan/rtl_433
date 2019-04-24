/* Chuango Security Technology Corporation
 * likely based on HS1527 or compatible
 *
 * Tested devices:
 * G5 GSM/SMS/RFID Touch Alarm System (Alarm, Disarm, ...)
 * DWC-100 Door sensor (Default: Normal Zone)
 * DWC-102 Door sensor (Default: Normal Zone)
 * KP-700 Wireless Keypad (Arm, Disarm, Home Mode, Alarm!)
 * PIR-900 PIR sensor (Default: Home Mode Zone)
 * RC-80 Remote Control (Arm, Disarm, Home Mode, Alarm!)
 * SMK-500 Smoke sensor (Default: 24H Zone)
 * WI-200 Water sensor (Default: 24H Zone)
 *
 * Note: simple 24 bit fixed ID protocol (x1527 style) and should be handled by the flex decoder.
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "decoder.h"

static int chuango_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    uint8_t *b;
    int id;
    int cmd;
    char *cmd_str;

    if (bitbuffer->bits_per_row[0] != 25)
        return 0;
    b = bitbuffer->bb[0];

    b[0] = ~b[0];
    b[1] = ~b[1];
    b[2] = ~b[2];

    // Validate package
    if (!(b[3] & 0x80)    // Last bit (MSB here) is always 1
        || !b[0] || !b[1] || !(b[2] & 0xF0))    // Reduce false positives. ID 0x00000 not supported
        return 0;

    id = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4); // ID is 20 bits (Ad: "1 Million combinations" :-)
    cmd = b[2] & 0x0F;

    switch(cmd) {
        case 0xF: cmd_str = "?"; break;
        case 0xE: cmd_str = "?"; break;
        case 0xD: cmd_str = "Low Battery"; break;
        case 0xC: cmd_str = "?"; break;
        case 0xB: cmd_str = "24H Zone"; break;
        case 0xA: cmd_str = "Single Delay Zone"; break;
        case 0x9: cmd_str = "?"; break;
        case 0x8: cmd_str = "Arm"; break;
        case 0x7: cmd_str = "Normal Zone"; break;
        case 0x6: cmd_str = "Home Mode Zone";    break;
        case 0x5: cmd_str = "On"; break;
        case 0x4: cmd_str = "Home Mode"; break;
        case 0x3: cmd_str = "Tamper";    break;
        case 0x2: cmd_str = "Alarm"; break;
        case 0x1: cmd_str = "Disarm";    break;
        case 0x0: cmd_str = "Test"; break;
        default:  cmd_str = ""; break;
    }

    data = data_make(
            "model",    "",             DATA_STRING, _X("Chuango-Security","Chuango Security Technology"),
            "id",       "ID",           DATA_INT,    id,
            "cmd",      "CMD",          DATA_STRING, cmd_str,
            "cmd_id",   "CMD_ID",       DATA_INT,    cmd,
            NULL);

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "cmd",
    "cmd_id",
    NULL
};

r_device chuango = {
    .name           = "Chuango Security Technology",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 568,  // Pulse: Short 568µs, Long 1704µs
    .long_width     = 1704, // Gaps:  Short 568µs, Long 1696µs
    .reset_limit    = 1800, // Intermessage Gap 17200µs (individually for now)
    .sync_width     = 0,    // No sync bit used
    .tolerance      = 160,  // us
    .decode_fn      = &chuango_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
