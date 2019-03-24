/* Protocol of the SimpliSafe Sensors
 *
 * The data is sent leveraging a PiWM Encoding where a long is 1, and a short is 0
 *
 * All bytes are sent with least significant bit FIRST (1000 0111 = 0xE1)
 *
 *  2 Bytes   | 1 Byte       | 5 Bytes   | 1 Byte  | 1 Byte  | 1 Byte       | 1 Byte
 *  Sync Word | Message Type | Device ID | CS Seed | Command | SUM CMD + CS | Epilogue
 *
 * Copyright (C) 2018 Adam Callis <adam.callis@gmail.com>
 * License: GPL v2+ (or at your choice, any other OSI-approved Open Source license)
 */

#include "decoder.h"

static void
ss_get_id(char *id, uint8_t *b)
{
    char *p = id;

    // Change to least-significant-bit last (protocol uses least-siginificant-bit first) for hex representation:
    for (uint16_t k = 3; k <= 7; k++) {
        b[k] = reverse8(b[k]);
        sprintf(p++, "%c", (char)b[k]);
    }
    *p = '\0';
}

static int
ss_sensor_parser(r_device *decoder, bitbuffer_t *bitbuffer, int row)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[row];
    char id[6];
    char extradata[30] = "";

    // each row needs to have exactly 92 bits
    if (bitbuffer->bits_per_row[row] != 92)
        return 0;

    uint8_t seq = reverse8(b[8]);
    uint8_t state = reverse8(b[9]);
    uint8_t csum = reverse8(b[10]);
    if (((seq + state) & 0xff) != csum) return 0;

    ss_get_id(id, b);

    if (state == 1) {
        strcpy(extradata,"Contact Open");
    } else if (state == 2) {
        strcpy(extradata,"Contact Closed");
    } else if (state == 3) {
        strcpy(extradata,"Alarm Off");
    }

    data = data_make(
            "model",        "",             DATA_STRING, _X("SimpliSafe-Sensor","SimpliSafe Sensor"),
            _X("id","device"),       "Device ID",    DATA_STRING, id,
            "seq",          "Sequence",     DATA_INT, seq,
            "state",        "State",        DATA_INT, state,
            "extradata",    "Extra Data",   DATA_STRING, extradata,
            NULL
    );
    decoder_output_data(decoder, data);

    return 1;
}

static int
ss_pinentry_parser(r_device *decoder, bitbuffer_t *bitbuffer, int row)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[row];
    char id[6];
    char extradata[30];
    // In a keypad message the pin is encoded in bytes 10 and 11 with the the digits each using 4 bits
    // However the bits are low order to high order
    int digits[5];
    int pina = reverse8(b[10]);
    int pinb = reverse8(b[11]);

    digits[0] = (pina & 0xf);
    digits[1] = ((pina & 0xf0) >> 4);
    digits[2] = (pinb & 0xf);
    digits[3] = ((pinb & 0xf0) >> 4);

    ss_get_id(id, b);

    sprintf(extradata, "Disarm Pin: %x%x%x%x", digits[0], digits[1], digits[2], digits[3]);

    data = data_make(
            "model",        "",             DATA_STRING, _X("SimpliSafe-Keypad","SimpliSafe Keypad"),
            _X("id","device"),       "Device ID",    DATA_STRING, id,
            "seq",          "Sequence",     DATA_INT, b[9],
            "extradata",    "Extra Data",   DATA_STRING, extradata,
            NULL
    );
    decoder_output_data(decoder, data);

    return 1;
}

static int
ss_keypad_commands(r_device *decoder, bitbuffer_t *bitbuffer, int row)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[row];
    char id[6];
    char extradata[30]; // = "Arming: ";

    if (b[10] == 0x6a) {
        strcpy(extradata, "Arm System - Away");
    } else if (b[10] == 0xca) {
        strcpy(extradata, "Arm System - Home");
    } else if (b[10] == 0x3a) {
        strcpy(extradata, "Arm System - Cancelled");
    } else if (b[10] == 0x2a) {
        strcpy(extradata, "Keypad Panic Button");
    } else if (b[10] == 0x86) {
        strcpy(extradata, "Keypad Menu Button");
    } else {
        sprintf(extradata, "Unknown Keypad: %02x", b[10]);
    }

    ss_get_id(id, b);

    data = data_make(
            "model",        "",             DATA_STRING, _X("SimpliSafe-Keypad","SimpliSafe Keypad"),
            _X("id","device"),       "Device ID",    DATA_STRING, id,
            "seq",          "Sequence",     DATA_INT, b[9],
            "extradata",    "Extra Data",   DATA_STRING, extradata,
            NULL
    );
    decoder_output_data(decoder, data);

    return 1;
}

static int
ss_sensor_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Require two identical rows.
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 90);
    if (row < 0) return 0;

    // The row must start with 0xcc5f (0x33a0 inverted).
    uint8_t *b = bitbuffer->bb[row];
    if (b[0] != 0xcc || b[1] != 0x5f) return 0;

    bitbuffer_invert(bitbuffer);

    if (b[2] == 0x88) {
        return ss_sensor_parser(decoder, bitbuffer, row);
    } else if (b[2] == 0x66) {
        return ss_pinentry_parser(decoder, bitbuffer, row);
    } else if (b[2] == 0x44) {
        return ss_keypad_commands(decoder, bitbuffer, row);
    } else {
        if (decoder->verbose)
            fprintf(stderr, "Unknown Message Type: %02x\n", b[2]);
        return 0;
    }
}

static char *sensor_output_fields[] = {
    "model",
    "device", // TODO: delete this
    "id",
    "seq",
    "state",
    "extradata",
    NULL
};

r_device ss_sensor = {
    .name           = "SimpliSafe Home Security System (May require disabling automatic gain for KeyPad decodes)",
    .modulation     = OOK_PULSE_PIWM_DC,
    .short_width    = 500,  // half-bit width 500 us
    .long_width     = 1000, // bit width 1000 us
    .reset_limit    = 2200,
    .tolerance      = 100, // us
    .decode_fn      = &ss_sensor_callback,
    .disabled       = 0,
    .fields         = sensor_output_fields
};
