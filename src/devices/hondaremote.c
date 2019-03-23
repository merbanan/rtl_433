/* Honda Car Key
 *
 * Identifies button event, but does not attempt to decrypt rolling code...
 * Note that this is actually Manchester coded and should be changed.
 *
 * Copyright (C) 2016 Adrian Stevenson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

static char const *command_code[] = {"boot", "unlock" , "lock",};

static char const *get_command_codes(const uint8_t *bytes)
{
    unsigned char command = bytes[46] - 0xAA;
    if (command < (sizeof(command_code)/sizeof(command_code[0]))) {
        return command_code[command];
    } else {
        return "unknown";
    }
}

static int hondaremote_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    char const *code;
    uint16_t device_id;

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        b = bitbuffer->bb[row];
        // Validate package
        if (((bitbuffer->bits_per_row[row] <= 385) || (bitbuffer->bits_per_row[row] > 394)) ||
                ((b[0] != 0xFF ) || (b[38] != 0xFF)))
            continue;

        code = get_command_codes(b);
        device_id = b[44]<<8 | b[45];

        data = data_make(
                "model",        "",     DATA_STRING, _X("Honda-CarRemote","Honda Remote"),
                _X("id","device id"),    "",    DATA_INT, device_id,
                "code",         "",    DATA_STRING, code,
                NULL);

        decoder_output_data(decoder, data);
        return 1;
    }
    return 0;
}

static char *output_fields[] = {
    "model",
    "device_id", // TODO: delete this
    "id",
    "code",
    NULL
};

r_device hondaremote = {
    .name           = "Honda Car Key",
    .modulation     = FSK_PULSE_PWM,
    .short_width    = 250,
    .long_width     = 500,
    .reset_limit    = 2000,
    .decode_fn      = &hondaremote_callback,
    .disabled       = 0,
    .fields         = output_fields
};
