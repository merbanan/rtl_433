/** @file
    TR-502MSV remote controller for RC-710DX.

    Copyright (C) 2024 Filip Kosecek <filip.kosecek@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define RESERVED_BIT   2
#define BRIGHTNESS_BIT 3
#define SWITCH_BIT     4
#define PREAMBLE_BIT   20

enum socket_codes {
    SOCKET_ONE   = 0x0,
    SOCKET_TWO   = 0x4,
    SOCKET_THREE = 0x2,
    SOCKET_FOUR  = 0x6,
    SOCKET_ALL   = 0x7
};

/* Determine the target socket. */
static const char *get_sock_id(uint32_t packet)
{
    packet = (packet >> 5) & 0x7;

    switch (packet) {
    case SOCKET_ONE:
        return "1";
    case SOCKET_TWO:
        return "2";
    case SOCKET_THREE:
        return "3";
    case SOCKET_FOUR:
        return "4";
    case SOCKET_ALL:
        return "ALL";
    default:
        return NULL;
    }
}

/* Parse the command, i.e. set brightness or power on/off. */
static const char *get_command(uint32_t packet)
{
    int on, brightness;

    on         = packet & (1 << SWITCH_BIT);
    brightness = packet & (1 << BRIGHTNESS_BIT);

    if (on) {
        if (brightness)
            return "DIM";
        return "ON";
    }
    if (brightness)
        return "BRIGHT";
    return "OFF";
}

/* Join the three bytes into one 4-byte integer. */
static uint32_t build_packet(uint8_t *data)
{
    uint32_t packet = 0;

    for (unsigned int i = 0; i <= 2; ++i) {
        packet <<= 8;
        packet |= data[i];
    }

    /* The third byte only contains 5 bits. */
    packet >>= 3;
    return packet;
}

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
    uint32_t packet;
    int device_id;
    data_t *output_data;
    const char *command, *sock_id;

    if (buffer->num_rows != 1 || buffer->bits_per_row[0] != 21)
        return DECODE_ABORT_LENGTH;

    packet = build_packet(buffer->bb[0]);
    if ((packet & (1 << PREAMBLE_BIT)) == 0)
        return DECODE_ABORT_EARLY;
    if ((packet & (1 << RESERVED_BIT)) != 0)
        return DECODE_FAIL_SANITY;

    device_id = (packet & ~(1 << PREAMBLE_BIT)) >> 8;
    command   = get_command(packet);
    sock_id   = get_sock_id(packet);
    if (sock_id == NULL)
        return DECODE_FAIL_SANITY;

    /* clang-format off */
    output_data = data_make (
        "model",    "Model",        DATA_STRING,    "TR-502MSV",
        "id",    "Device ID",        DATA_FORMAT,    "%u",    DATA_INT,    device_id,
        "sock_id",    "Socket",    DATA_STRING,    sock_id,
        "command",    "Command",    DATA_STRING,    command,
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
        "sock_id",
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
