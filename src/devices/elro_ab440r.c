/** @file
    ELRO AB440R remote control.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define PAYLOAD_BIT_LENGTH 25

// inspired by:
// https://www.geeksforgeeks.org/write-an-efficient-c-program-to-reverse-bits-of-a-number/
static uint8_t reverse_5bit_int(uint8_t num) {
    uint8_t reverse_num = 0;
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if ((num & (1 << i)))
            reverse_num |= 1 << ((4) - i);
    }
    return reverse_num;
}

// takes every second bit and stops after 12 bits
static void decode_bit_payload(bitrow_t row, uint16_t *bit_payload) {
    uint8_t target_index;
    uint8_t row_index;
    uint8_t bit_pos;
    uint8_t source_bit;

    (*bit_payload) = 0; // initialize payload, otherwise it returned 0xFFFF for me.

    for(target_index = 0; target_index < 12; target_index++) {
        row_index = 2 - (target_index / 4);
        bit_pos = (target_index * 2) % 8;
        source_bit = (row[row_index] & (1 << bit_pos));

        (*bit_payload) |= (source_bit << target_index) >> bit_pos;
    }
}

/**
ELRO AB440R remote control.
Remote switch to turn on or off power sockets.
The remote control has 8 buttons to control 4 sockets (on and off button)
and 5 dip switches to dial in a unique local channel (0-31)

User manual: https://www.libble.eu/elro-ab440-series/online-manual-313854/

Payload format:

Payload: 1C1C1C1C1C 1B1B1B1B 10 1S1S 10000000

CCCCC: 5 bit channel number (reversed)
BBBB:  1000 = button A
       0100 = button B
       0010 = button C
       0001 = button D
SS:    10 = ON
       01 = OFF
*/
static int elro_ab440r_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    uint8_t *row;
    uint16_t decoded_payload;
    uint8_t button_name_section;
    uint8_t button_state_section;
    uint8_t channel_section;

    char *button_name;
    char *button_state;

    if (bitbuffer->bits_per_row[0] != PAYLOAD_BIT_LENGTH)
        return DECODE_ABORT_LENGTH;

    row = bitbuffer->bb[0];

    // All odd bits need to be ones
    // 0xAA = 10101010
    // last row item is 0x80 in all cases
    if (
      (row[0] & 0xAA) != 0xAA ||
      (row[1] & 0xAA) != 0xAA ||
      (row[2] & 0xAA) != 0xAA ||
      row[3] != 0x80
    ) return DECODE_FAIL_MIC;

    decode_bit_payload(row, &decoded_payload);

    button_name_section = (decoded_payload & 0x78) >> 3; // bits 3-6

    switch (button_name_section) {
        case 0x8: button_name = "A"; break;
        case 0x4: button_name = "B"; break;
        case 0x2: button_name = "C"; break;
        case 0x1: button_name = "D"; break;
        default: return DECODE_FAIL_SANITY;
    }

    button_state_section = decoded_payload & 0x3; // first 2 bits

    switch(button_state_section) {
        case 0x2: button_state = "on"; break;
        case 0x1: button_state = "off"; break;
        default: return DECODE_FAIL_SANITY;
    }

    channel_section = reverse_5bit_int((decoded_payload & 0xF80) >> 7); // bits 7-11

    /* clang-format off */
    data = data_make(
            "model",    "",        DATA_STRING, "ELRO Home control system",
            "button",   "Button",  DATA_STRING, button_name,
            "state",    "State",   DATA_STRING, button_state,
            "channel",  "Channel", DATA_INT, channel_section,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "button",
        "state",
        "channel",
        NULL,
};

r_device elro_ab440r = {
        .name        = "ELRO Home control system",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 330,
        .long_width  = 970,
        .gap_limit   = 1200,
        .reset_limit = 9000,
        .decode_fn   = &elro_ab440r_callback,
        .fields      = output_fields,
};
