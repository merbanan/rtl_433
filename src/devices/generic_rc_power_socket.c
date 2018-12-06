/* Generic remote controlled power sockets using PT2262/PT2272 (or equivalent) protocol.
 *
 * This is called 'generic' because there are so many clones of this particular device and this code
 * will work with many of them, but still keep in mind not all of them are compatible even if they
 * are built around PT2262/PT2272 or similar ICs because product designers sometimes get creative and use
 * a different allocation of the address/data bits of the 2262 chip or a different timing resistor.
 * The potential differences are usually around the way the DIP switches are used in the design. There can
 * be differences in the actual number of bits allocated for remote id and the socket id fields, and there
 * can also be differences in the order of bits in these fields. In most cases designers use 2-state DIP
 * switches instead of tri-state so not all 3 allowable protocol states (0, 1, Z) are actually used. Also,
 * the meaning of the DIP switches is not necessarily 0/1 as you would probably assume, instead they could
 * be inverted or a 0/1 DIP bit value could be mapped to a Z transmitted value instead, and finally, there
 * can be differences in how the on/off command is sent. Implementations usually use two bits but some might
 * use only one.
 *
 * The TH 111 device uses 2-state DIP switches, 5 for remote id mapped to A0-A4 (all can be set or unset)
 * and 5 for socket id mapped to A5, D5, D4, D3, D2 (only one can be set). The on/off command is sent via
 * two individual bits (D1, D0). The mapping of the DIP switches and button values to actuall transmitted values
 * is not the intuitive one, so here we go:
 *     Remote id: 0 -> Z,
 *                1 -> 1
 *
 *     Socket id: 1 -> 0,
 *                0 -> Z
 *
 *     Command:   On -> D1=Z,D0=0,
 *                Off -> D1=0,D0=Z
 *
 * NOTE: this protcol uses 25 bit packets and might clash with other protocols using same packet length, notably other
 * 2262/2272 decoders and also seems to be similar enough to 'WS Temperature Sensor' leading to annoying duplicate
 * matches. Please disable those protocols manually to avoid such annoyances.
 *
 * Tested devices:
 * - Somogyi Elektronic TH 111 (with HX2262/HX2272) (see https://www.somogyi.hu/product/taviranyithato-halozati-aljzat-szett-1db-aljzat-1db-taviranyito-th-111-12121)
 *   Product pictures indicate strongly this might be a clone of the (yet untested) Avidsen indoor remote-controlled socket (see http://www.avidsen.com/product-sav?product_id=312&lang=en_US)
 *
 * Copyright (C) 2018 Adrian Nistor
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

static int generic_rc_power_socket_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    const uint8_t *bb = bitbuffer->bb[0];

    // validate package length and presence of last 'sync' bit
    if ((bitbuffer->bits_per_row[0] != 25) || (bb[3] & 0x80) == 0) // last bit (MSB here) must always be 1 according to protocol (a 0 here due to inversion)
        return 0;

    // the signal should have at least 2 repeats
    int r = bitbuffer_find_repeated_row(bitbuffer, 2, 25);
    if (r < 0)
        return 0;

    // we have a good candidate, so print it if we are verbose
    if (decoder->verbose) {
	fprintf(stdout, "%s: Attempting decode of:\n", __func__);
	bitbuffer_print(bitbuffer);
	fprintf(stdout, "\n");
    }

    // invert the bits, short pulse is 0, long pulse is 1
    const uint32_t packet = ~(bb[0] << 16 | bb[1] << 8 | bb[2]);

    // Seems OKish up to now, so start decoding the packet and filter out eventual broken packets. These could be broken due to RF interference,
    // multiple button pressed simultaneosly by the user, or due to improper button debouncing in this oversimplified electrical design.

    // decode the On(1) / Off(0) command
    uint8_t cmd = 0;
    const uint8_t cmdTriBits = packet & 0x0F;
    switch (cmdTriBits) {
        case 0x04: cmd = 1; break;   // on

        case 0x01: break;            // off

        default:
          // both command bits or no command bits were set, invalid packet, possibly because both on/off
          // buttons were pressed together or because of lack of button debouncing
          if (decoder->verbose)
                fprintf(stderr, "%s: Invalid command tri-bits: %d\n", __func__, cmdTriBits);
          return 0;
    }

    // decode the remote id (any 5 bit wide value is valid, including 0, leading to 32 unique ids)
    uint8_t remoteId = 0;
    for (int i = 0; i < 5; i++) {
        const uint8_t addressTriBit = (packet >> (14 +  i * 2)) & 0x03;
        switch (addressTriBit) {
            case 0x01: break;                         // 'floating' in remote id field means DIP switch set to 0

            case 0x03: remoteId |= (1 << i); break;   // 1 means DIP switch set to 1

            default:                                  // found an illegal bit combination, give up
                if (decoder->verbose)
                    fprintf(stderr, "%s: Invalid address tri-bit: %d\n", __func__, addressTriBit);
                return 0;
        }
    }

    // decode the socket id (button name): A to E, represented as a number 1 to 5; exactly one button must be pressed
    uint8_t socketId = 0;
    for (int i = 0; i < 5; i++) {
        const uint8_t dataTriBit = (packet >> (4 + i * 2)) & 0x03;
        switch (dataTriBit) {
            case 0x00: {                          // a transmitted value of 0 actually means a socket id DIP switch set to 1
                if (socketId != 0)
                    return 0;                     // found multiple buttons pressed toghether, invalid packet, give up
                socketId = 5 - i;
                break;
            }

            case 0x01: break;                     // 'floating' in socket id field means DIP switch set to 0

            default:                              // found an illegal bit combination, give up
                if (decoder->verbose)
                    fprintf(stderr, "%s: Invalid data tri-bit: %d\n", __func__, dataTriBit);
                return 0;
        }
    }

    if (socketId == 0) {                          // it is an error to not have any button flag set, give up
        if (decoder->verbose)
            fprintf(stderr, "%s: Invalid socketId 0\n", __func__);
        return 0;
    }

    // output tri-state for debugging purposes: 12 tri-bits
    char tristate[13];
    char *p = tristate;
    for (int i = 22; i >= 0; i -= 2) {
        switch ((packet >> i) & 0x03) {
            case 0x00: *p++ = '0'; break;

            case 0x01: *p++ = 'Z'; break; // floating

            case 0x03: *p++ = '1'; break;

            case 0x02: *p++ = '?'; break; // this is illegal but not really possible
        }
    }
    *p = '\0';

    data_t *data = data_make(
            "model",        "",             DATA_STRING, "Generic Remote Controlled Power Socket (PT2262/PT2272)",
            "remoteId",     "Remote Id",    DATA_INT,    remoteId,
            "socketId",     "Socket Id",    DATA_INT,    socketId,
            "cmd",          "Command",      DATA_INT,    cmd,
            "tristate",     "Tri-State",    DATA_STRING, tristate,
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "remoteId",
    "socketId",
    "cmd",
    "tristate",
    NULL
};

// the timings were determined by signal analysis, not by IC data sheet, so these might be slightly off but they work nicely
r_device generic_rc_power_socket = {
    .name           = "Generic Remote Controlled Power Socket (PT2262/PT2272)",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 160,
    .long_width     = 440,
    .reset_limit    = 4336,
    .gap_limit      = 412,
    .sync_width     = 0,    // No sync bit is used
    .tolerance      = 150,  // microseconds
    .fields         = output_fields,
    .decode_fn      = &generic_rc_power_socket_callback,
    .disabled       = 0
};

