/** @file
    TR-502MSV remote controller for RC-710DX.

    Copyright (C) 2024 Filip Kosecek <filip.kosecek@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
TR-502MSV remote controller for RC-710DX.

21-bit data packet format, repeated up to 4 times
    PIIIIIII IIIIISSS OCRUU

- P: 1-bit preamble
- I: 12-bit device id
- S: 3-bit socket id
- O: 1-bit on/off
- C: 1-bit command - brightness/switch
- R: 1 reserved bit (always 0)
- U: 2 unknown bits, most likely a checksum

*/

static int tr502msv_decode(r_device *decoder, bitbuffer_t *buffer)
{
    int device_id;
    data_t *output_data;
    uint8_t socket_id;
    uint8_t *b;
    const char *command_str, *socket_str;
    const char *sockets[]  = {"1", "3", "2", "4", "ALL"};
    const char *commands[] = {"OFF", "BRIGHT", "ON", "DIM"};

    if (buffer->num_rows != 1 || buffer->bits_per_row[0] != 21)
        return DECODE_ABORT_LENGTH;

    b = buffer->bb[0];
    if ((b[0] & (1 << 7)) == 0)
        return DECODE_ABORT_EARLY;
    if ((b[2] & (1 << 5)) != 0)
        return DECODE_FAIL_SANITY;

    device_id   = ((b[0] & 0x7f) << 5) | (b[1] >> 3);
    command_str = commands[b[2] >> 6];
    socket_id   = b[1] & 0x7;
    if (socket_id % 2 == 0)
        socket_str = sockets[socket_id >> 1];
    else if (socket_id == 0x7)
        socket_str = sockets[4];
    else
        return DECODE_FAIL_SANITY;

    /* clang-format off */
    output_data = data_make (
        "model",    "Model",        DATA_STRING,    "TR-502MSV",
        "id",    "Device ID",        DATA_FORMAT,    "%u",    DATA_INT,    device_id,
        "socket_id",    "Socket",    DATA_STRING,    socket_str,
        "command",    "Command",    DATA_STRING,    command_str,
        NULL
        );
    /* clang-format on */
    decoder_output_data(decoder, output_data);

    return 1;
}

static const char *output_fields[] = {
        "model",
        "id",
        "device_id",
        "socket_id",
        "command",
        NULL,
};

const r_device tr_502msv = {
        .name        = "TR-502MSV remote smart socket controller",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 740,
        .long_width  = 1400,
        .tolerance   = 70,
        .reset_limit = 84000,
        .decode_fn   = tr502msv_decode,
        .fields      = output_fields,
};
