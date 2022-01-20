/** @file
    Decoder for Regency fan remotes

    Copyright (C) 2020-2022 David E. Tiller

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/**
 The device uses OOK_PULSE_PPM encoding.
 The packet starts with 576 uS start pulse.
 - 0 is defined as a 375 uS gap followed by a 970 uS pulse.
 - 1 is defined as a 880 uS gap followed by a 450 uS pulse.

 Transmissions consist of the start bit followed by bursts of 20 bits.
 These packets ar repeated up to 11 times.

 As written, the PPM code always interpets a narrow gap as a 1 and a
 long gap as a 0, however the actual data over the air is inverted,
 i.e. a short gap is a 0 and a long gap is a 1. In addition, the data
 is 5 nibbles long and is represented in Little-Endian format. In the
 code I invert the bits and also reflect the bytes. Reflection introduces
 an additional nibble at bit offsets 16-19, so the data is expressed a 3
 complete bytes.

 The examples below are _after_ inversion and reflection (MSB's are on
 the left).

 Packet layout
 Bit number
 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23
  CHANNEL  |  COMMAND  |            VALUE       | 0  0  0  0| 4 bit checksum

 CHANNEL is determined by the bit switches in the battery compartment. All
 switches in the 'off' position result in a channel of 15, implying that the
 switches pull the address lines down when in the on position.

 COMMAND is one of the following:

 CMD_STOP        0x01
        value: (0xc0, unused).

 CMD_FAN_SPEED   0x02
        value: 0x01-0x07. On my remote, the speeds are shown as 8 - value.

 CMD_LIGHT_INT   0x04
        value: 0x00-0xc3. The value is the intensity percentage.
               0x00 id off, 0xc3 is 99% (full).

 CMD_LIGHT_DELAY 0x05
        value: 0x00 is 'off', 0x01 is on.

 CMD_FAN_DIR     0x06
        value: 0x07 is one way, 0x83 is the other.

 The CHECKSUM is calculated by adding the nibbles of the first two bytes
 and ANDing the result with 0x0f.

 */

#include <stdlib.h>
#include "decoder.h"

#define NUM_BITS        21
#define NUM_BYTES       3
#define BYTE_START      1

#define CMD_CHAN_BYTE   0
#define VALUE_BYTE      1
#define SUM_BYTE        2

#define CMD_STOP        1
#define CMD_FAN_SPEED   2
#define CMD_LIGHT_INT   4
#define CMD_LIGHT_DELAY 5
#define CMD_FAN_DIR     6

static char *command_names[] = {
    /* 0  */ "invalid",
    /* 1  */ "fan_speed",
    /* 2  */ "fan_speed",
    /* 3  */ "invalid",
    /* 4  */ "light_intensity",
    /* 5  */ "light_delay",
    /* 6  */ "fan_direction",
    /* 7  */ "invalid",
    /* 8  */ "invalid",
    /* 9  */ "invalid",
    /* 10 */ "invalid",
    /* 11 */ "invalid",
    /* 12 */ "invalid",
    /* 13 */ "invalid",
    /* 14 */ "invalid",
    /* 15 */ "invalid"
};

static int regency_fan_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{

    data_t *data = NULL;
    int index = 0; // a row index
    int num_bits = 0;
    int debug_output = decoder->verbose;
    int return_code = 0;

    if (debug_output > 1) {
        bitbuffer_printf(bitbuffer, "%s: ", __func__);
    }

    if (bitbuffer->num_rows < 1) {
        if (debug_output > 1) {
            fprintf(stderr, "No rows.\n");
        }

        return 0;
    }

    bitbuffer_invert(bitbuffer);

    for (index = 0; index < bitbuffer->num_rows; index++) {
        num_bits = bitbuffer->bits_per_row[index];

        if (num_bits != NUM_BITS) {
            if (debug_output > 1) {
                fprintf(stderr, "Expected %d bits, got %d.\n", NUM_BITS, num_bits);
            }

            continue;
        }

        uint8_t bytes[NUM_BYTES];
        bitbuffer_extract_bytes(bitbuffer, index, BYTE_START, bytes, NUM_BITS);
        reflect_bytes(bytes, NUM_BYTES);

        // Calculate nibble sum and compare
        int checksum = add_nibbles(bytes, 2) & 0x0f;
        if (checksum != bytes[SUM_BYTE]) {
            if (debug_output > 1) {
                fprintf(stderr, "Checksum failure: expected %0x, got %0x\n", bytes[SUM_BYTE], checksum);
            }

            continue;
        }

        /*
         * Now that message "envelope" has been validated, start parsing data.
         */
        uint8_t command = bytes[CMD_CHAN_BYTE] >> 4;
        uint8_t channel = ~bytes[CMD_CHAN_BYTE] & 0x0f;
        uint32_t value = bytes[VALUE_BYTE];
        char value_string[64] = {0};

        switch(command) {
            case CMD_STOP:
                sprintf(value_string, "stop");
                break;

            case CMD_FAN_SPEED:
                sprintf(value_string, "speed %d", value);
                break;

            case CMD_LIGHT_INT:
                sprintf(value_string, "%d %%", value);
                break;

            case CMD_LIGHT_DELAY:
                sprintf(value_string, "%s", value == 0 ? "off" : "on");
                break;

            case CMD_FAN_DIR:
                sprintf(value_string, "%s", value == 0x07 ? "clockwise" : "counter-clockwise");
                break;

            default:
                if (debug_output > 1) {
                    fprintf(stderr, "Unknown command: %d\n", command);
                    continue;
                }
                break;
        }

        return_code = 1;

        data = data_make(
        "model",            "",     DATA_STRING,    "Regency-compatible Remote",
        "type",             "",     DATA_STRING,    "Ceiling Fan",
        "channel",          "",     DATA_INT,       channel,
        "command",          "",     DATA_STRING,    command_names[command],
        "value",            "",     DATA_STRING,    value_string,
        "mic",              "",     DATA_STRING,    "nibble_sum",
        NULL);

        decoder_output_data(decoder, data);
    }

    // Return 1 if message successfully decoded
    return return_code;
}

/*
 * List of fields that may appear in the output
 *
 * Used to determine what fields will be output in what
 * order for this device when using -F csv.
 *
 */
static char *output_fields[] = {
    "model",
    "type",
    "channel",
    "command",
    "value",
    "mic",
    NULL,
};

r_device regency_fan = {
    .name        = "Regency Ceiling Fan Remote (-f 303.75M to 303.96M)",
    .modulation  = OOK_PULSE_PWM,
    .short_width = 580,
    .long_width  = 976,
    .gap_limit   = 8000,
    .reset_limit = 14000,
    .decode_fn   = &regency_fan_decode,
    .disabled    = 0, // disabled and hidden, use 0 if there is a MIC, 1 otherwise
    .fields      = output_fields,
};
