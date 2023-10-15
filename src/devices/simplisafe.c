/** @file
    Protocol of the SimpliSafe Sensors.

    Copyright (C) 2018 Adam Callis <adam.callis@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    License: GPL v2+ (or at your choice, any other OSI-approved Open Source license)
*/
/** @fn int ss_sensor_callback(r_device *decoder, bitbuffer_t *bitbuffer)
Protocol of the SimpliSafe Sensors.

@sa ss_sensor_parser()
@sa ss_pinentry_parser()
@sa ss_keypad_commands()

The data is sent leveraging a PiWM Encoding where a long is 1, and a short is 0

All bytes are sent with least significant bit FIRST (1000 0111 = 0xE1)

 2 Bytes   | 1 Byte       | 5 Bytes   | 1 Byte  | 1 Byte  | 1 Byte       | 1 Byte
 Sync Word | Message Type | Device ID | CS Seed | Command | SUM CMD + CS | Epilogue

*/

#include "decoder.h"

static void ss_get_id(char *id, uint8_t *b)
{
    char *p = id;

    // Change to least-significant-bit last (protocol uses least-significant-bit first) for hex representation:
    for (uint16_t k = 3; k <= 7; k++) {
        char c = b[k];
        c = reverse8(c);
        // If the character is not representable with a valid-ish ascii character, replace with ?.
        // This probably means the message is invalid.
        // This is at least better than spitting out non-printable stuff :).
        if (c < 32 || c > 126) {
          sprintf(p++, "%c", '?');
          continue;
        }
        sprintf(p++, "%c", (char)c);
    }
    *p = '\0';
}

/**
SimpliSafe protocol for sensors.
*/
static int ss_sensor_parser(r_device *decoder, bitbuffer_t *bitbuffer, int row)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[row];

    // each row needs to have exactly 92 bits
    if (bitbuffer->bits_per_row[row] != 92)
        return DECODE_ABORT_LENGTH;

    uint8_t seq = reverse8(b[8]);
    uint8_t state = reverse8(b[9]);
    uint8_t csum = reverse8(b[10]);
    if (((seq + state) & 0xff) != csum)
      return DECODE_FAIL_MIC;

    char id[6];
    ss_get_id(id, b);

    char extradata[30];
    if (state == 1) {
        snprintf(extradata, sizeof(extradata), "Contact Open");
    } else if (state == 2) {
        snprintf(extradata, sizeof(extradata), "Contact Closed");
    } else if (state == 3) {
        snprintf(extradata, sizeof(extradata), "Alarm Off");
    } else {
        //snprintf(extradata, sizeof(extradata), "");
        *extradata = '\0';
    }

    /* clang-format off */
    data = data_make(
            "model",        "",             DATA_STRING, "SimpliSafe-Sensor",
            "id",           "Device ID",    DATA_STRING, id,
            "seq",          "Sequence",     DATA_INT,    seq,
            "state",        "State",        DATA_INT,    state,
            "extradata",    "Extra Data",   DATA_STRING, extradata,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
SimpliSafe protocol for pinentry.
*/
static int ss_pinentry_parser(r_device *decoder, bitbuffer_t *bitbuffer, int row)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[row];
    // In a keypad message the pin is encoded in bytes 10 and 11 with the the digits each using 4 bits
    // However the bits are low order to high order
    int digits[5];
    int pina = reverse8(b[10]);
    int pinb = reverse8(b[11]);

    digits[0] = (pina & 0xf);
    digits[1] = ((pina & 0xf0) >> 4);
    digits[2] = (pinb & 0xf);
    digits[3] = ((pinb & 0xf0) >> 4);

    char id[6];
    ss_get_id(id, b);

    char extradata[30];
    snprintf(extradata, sizeof(extradata), "Disarm Pin: %x%x%x%x", digits[0], digits[1], digits[2], digits[3]);

    /* clang-format off */
    data = data_make(
            "model",        "",             DATA_STRING, "SimpliSafe-Keypad",
            "id",           "Device ID",    DATA_STRING, id,
            "seq",          "Sequence",     DATA_INT,    b[9],
            "extradata",    "Extra Data",   DATA_STRING, extradata,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
SimpliSafe protocol for keypad commands.
*/
static int ss_keypad_commands(r_device *decoder, bitbuffer_t *bitbuffer, int row)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[row];
    char extradata[30]; // = "Arming: ";

    if (b[10] == 0x6a) {
        snprintf(extradata, sizeof(extradata), "Arm System - Away");
    } else if (b[10] == 0xca) {
        snprintf(extradata, sizeof(extradata), "Arm System - Home");
    } else if (b[10] == 0x3a) {
        snprintf(extradata, sizeof(extradata), "Arm System - Canceled");
    } else if (b[10] == 0x2a) {
        snprintf(extradata, sizeof(extradata), "Keypad Panic Button");
    } else if (b[10] == 0x86) {
        snprintf(extradata, sizeof(extradata), "Keypad Menu Button");
    } else {
        snprintf(extradata, sizeof(extradata), "Unknown Keypad: %02x", b[10]);
    }

    char id[6];
    ss_get_id(id, b);

    /* clang-format off */
    data = data_make(
            "model",        "",             DATA_STRING, "SimpliSafe-Keypad",
            "id",           "Device ID",    DATA_STRING, id,
            "seq",          "Sequence",     DATA_INT,    b[9],
            "extradata",    "Extra Data",   DATA_STRING, extradata,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static int ss_sensor_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Require two identical rows.
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 90);
    if (row < 0)
        return DECODE_ABORT_EARLY;

    // The row must start with 0xcc5f (0x33a0 inverted).
    uint8_t *b = bitbuffer->bb[row];
    if (b[0] != 0xcc || b[1] != 0x5f)
        return DECODE_ABORT_EARLY;

    bitbuffer_invert(bitbuffer);

    if (b[2] == 0x88) {
        return ss_sensor_parser(decoder, bitbuffer, row);
    } else if (b[2] == 0x66) {
        return ss_pinentry_parser(decoder, bitbuffer, row);
    } else if (b[2] == 0x44) {
        return ss_keypad_commands(decoder, bitbuffer, row);
    } else {
        decoder_logf(decoder, 1, __func__, "Unknown Message Type: %02x", b[2]);
        return DECODE_ABORT_EARLY;
    }
}

static char const *const sensor_output_fields[] = {
        "model",
        "id",
        "seq",
        "state",
        "extradata",
        NULL,
};

r_device const ss_sensor = {
        .name        = "SimpliSafe Home Security System (May require disabling automatic gain for KeyPad decodes)",
        .modulation  = OOK_PULSE_PIWM_DC,
        .short_width = 500,  // half-bit width 500 us
        .long_width  = 1000, // bit width 1000 us
        .reset_limit = 2200,
        .tolerance   = 100, // us
        .decode_fn   = &ss_sensor_callback,
        .fields      = sensor_output_fields,
};
