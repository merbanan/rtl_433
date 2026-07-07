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

21-bit data packet, repeated up to 4 times:

    PIIIIIII IIIIISSS OCRUU

- P: 1-bit preamble, always 1
- I: 12-bit device id
- S: 3-bit socket id
- O: 1-bit on/off
- C: 1-bit command, brightness/switch
- R: 1-bit reserved, always 0
- U: 2-bit checksum

Socket ids map to sockets as follows:

| Socket | Socket id |
| ------ | --------- |
|   1    |    000    |
|   2    |    100    |
|   3    |    010    |
|   4    |    110    |
|  all   |    111    |

The 2-bit checksum U1U0 is computed from the on/off bit (O), the command
bit (C), and the socket id bits S2S1S0 (msb to lsb) as:

    U1 = C ^ S2 ^ S0
    U0 = O ^ S1
*/
static int tr502msv_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    char const *commands[] = {"OFF", "BRIGHT", "ON", "DIM"};
    char const *sockets[]  = {"1", "3", "2", "4", "ALL"};

    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] != 21) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[0];

    if ((b[0] & 0x80) == 0) { // preamble bit
        return DECODE_ABORT_EARLY;
    }
    if ((b[2] & 0x20) != 0) { // reserved bit
        return DECODE_FAIL_SANITY;
    }

    int device_id = ((b[0] & 0x7f) << 5) | (b[1] >> 3);
    int socket_id = b[1] & 0x07;
    int on_off    = (b[2] & 0x80) >> 7;
    int command   = (b[2] & 0x40) >> 6;
    int chk1      = (b[2] & 0x10) >> 4;
    int chk0      = (b[2] & 0x08) >> 3;

    int s2 = (socket_id >> 2) & 1;
    int s1 = (socket_id >> 1) & 1;
    int s0 = socket_id & 1;
    if (chk1 != (command ^ s2 ^ s0) || chk0 != (on_off ^ s1)) {
        return DECODE_FAIL_MIC;
    }

    char const *socket_str;
    if (socket_id % 2 == 0) {
        socket_str = sockets[socket_id >> 1];
    } else if (socket_id == 0x7) {
        socket_str = sockets[4];
    } else {
        return DECODE_FAIL_SANITY;
    }

    char const *command_str = commands[(on_off << 1) | command];

    /* clang-format off */
    data_t *data = data_make(
            "model",        "Model",        DATA_STRING, "TR-502MSV",
            "id",           "Device ID",    DATA_FORMAT, "%u", DATA_INT, device_id,
            "socket_id",    "Socket",       DATA_STRING, socket_str,
            "command",      "Command",      DATA_STRING, command_str,
            "mic",          "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "socket_id",
        "command",
        "mic",
        NULL,
};

r_device const tr_502msv = {
        .name        = "TR-502MSV remote smart socket controller",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 740,
        .long_width  = 1400,
        .tolerance   = 70,
        .reset_limit = 84000,
        .decode_fn   = &tr502msv_decode,
        .fields      = output_fields,
};
