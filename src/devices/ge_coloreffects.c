/** @file
    GE Color Effects Remote.

    Copyright (C) 2017 Luke Cyca <me@lukecyca.com>, Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
GE Color Effects Remote.

Previous work decoding this device:
 https://lukecyca.com/2013/g35-rf-remote.html
 http://www.deepdarc.com/2010/11/27/hacking-christmas-lights/
*/
#include "decoder.h"

// Frame preamble:
// 11001100 11001100 11001100 11001100 11001100 11111111 00000000
// c   c    c   c    c   c    c   c    c   c    f   f    0   0
static const unsigned char preamble_pattern[3] = {0xcc, 0xff, 0x00};

// Helper to access single bit (copied from bitbuffer.c)
static inline int bit(const uint8_t *bytes, unsigned bit)
{
    return bytes[bit >> 3] >> (7 - (bit & 7)) & 1;
}

/*
 * Decodes the following encoding scheme:
 * 10 = 0
 *  1100 = 1
 */
unsigned ge_decode(bitbuffer_t *inbuf, unsigned row, unsigned start, bitbuffer_t *outbuf)
{
    uint8_t *bits = inbuf->bb[row];
    unsigned int len = inbuf->bits_per_row[row];
    unsigned int ipos = start;

    while (ipos < len) {
        uint8_t bit1, bit2;

        bit1 = bit(bits, ipos++);
        bit2 = bit(bits, ipos++);

        if (bit1 == 1 && bit2 == 0) {
            bitbuffer_add_bit(outbuf, 0);
        } else if (bit1 == 1 && bit2 == 1) {
            // Get two more bits
            bit1 = bit(bits, ipos++);
            bit2 = bit(bits, ipos++);
            if (bit1 == 0 && bit2 == 0) {
                bitbuffer_add_bit(outbuf, 1);
            } else {
                break;
            }
        } else {
            break;
        }
    }

    return ipos;
}

static int ge_coloreffects_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned start_pos)
{
    data_t *data;
    bitbuffer_t packet_bits = {0};
    uint8_t device_id;
    uint8_t command;

    ge_decode(bitbuffer, row, start_pos, &packet_bits);
    //bitbuffer_print(&packet_bits);

    /* From http://www.deepdarc.com/2010/11/27/hacking-christmas-lights/
     * Decoded frame format is:
     *   Preamble
     *   Two zero bits
     *   6-bit Device ID (Can be modified by adding R15-R20 on the large PCB)
     *   8-bit Command
     *   One zero bit
     */

    // Frame should be 17 decoded bits (not including preamble)
    if (packet_bits.bits_per_row[0] != 17)
        return DECODE_ABORT_LENGTH;

    // First two bits must be 0
    if (*packet_bits.bb[0] & 0xc0)
        return DECODE_FAIL_SANITY;

    // Last bit must be 0
    if (bit(packet_bits.bb[0], 16) != 0)
        return DECODE_FAIL_SANITY;

    // Extract device ID
    // We want bits [2..8]. Since the first two bits are zero, we'll just take the entire first byte
    device_id = *packet_bits.bb[0];

    // Extract command from the second byte
    bitbuffer_extract_bytes(&packet_bits, 0, 8, &command, 8);

    char cmd[7];
    switch(command) {
        case 0x5a:  snprintf(cmd, sizeof(cmd), "change");  break;
        case 0xaa:  snprintf(cmd, sizeof(cmd), "on");      break;
        case 0x55:  snprintf(cmd, sizeof(cmd), "off");     break;
        default:
            snprintf(cmd, sizeof(cmd), "0x%x", command);
            break;
    }

    // Format data
    data = data_make(
        "model",         "",     DATA_STRING, _X("GE-ColorEffects","GE Color Effects Remote"),
        "id",            "",     DATA_FORMAT, "0x%x", DATA_INT, device_id,
        "command",       "",     DATA_STRING, cmd,
        NULL);

    decoder_output_data(decoder, data);
    return 1;

}

/**
GE Color Effects Remote.
@sa ge_coloreffects_decode()
*/
static int ge_coloreffects_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    unsigned bitpos = 0;
    int ret         = 0;
    int events      = 0;

    // Find a preamble with enough bits after it that it could be a complete packet
    // (if the device id and command were all zeros)
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, (uint8_t *)&preamble_pattern, 24)) + 57 <=
            bitbuffer->bits_per_row[0]) {
        ret = ge_coloreffects_decode(decoder, bitbuffer, 0, bitpos + 24);
        if (ret > 0)
            events += ret;
        bitpos++;
    }

    return events > 0 ? events : ret;
}

static char *output_fields[] = {
    "model",
    "id",
    "command",
    NULL
};

r_device ge_coloreffects = {
    .name           = "GE Color Effects",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 52,
    .long_width     = 52,
    .reset_limit    = 450, // Maximum gap size before End Of Message [us].
    .decode_fn      = &ge_coloreffects_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
