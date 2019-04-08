/** Norgo Energy NGE101
 *
 * Copyright (C) 2019 jamaron
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/**
The code is based on info and code from Jesper Hansen's pages (used with
his permission):
http://blog.bitheap.net/p/this-is-overview-of-data-norge-nge101.html


The signal is FM encoded with clock cycle around x us, using
inverted OOK_PULSE_DMC modulation, i.e.
- No level shift within the clock cycle translates to a logic 1
- One level shift within the clock cycle translates to a logic 0
Each clock cycle begins with a level shift

+---+   +---+   +-------+       +  high
|   |   |   |   |       |       |
|   |   |   |   |       |       |
+   +---+   +---+       +-------+  low
^       ^       ^       ^       ^  clock cycle
|   0   |   0   |   1   |   1   |  translates as

Each transmission is either 55 or 71 bits long.

Data is transmitted in pure binary values, LSbit first.

Energy meter transmits pulse duration and pulse count as separate messages.
Transmissions also includes channel code and device ID. The sensor transmits
every 43 seconds 2 packets (55 bit packet twice or 71 bit packet together
with 55 bit packet).

55 bit packet contents:
1111 1010 | 0000 1101 | 1010 1000 | 0000 1000 | 0000 0000 /
xxxx xxxx | fccc dddd | dddd tttt | tttt tttt | tttt tttu /
1010 1101 / 1010 000
pppp pppp / pppp ppp

x - constant
f - packet type (0 = 55 bit packet)
c - channel (LSbit first)
d - device ID (LSbit first)
t - time in 1/1024 seconds between the last two impulses (LSbit first)
u - unknown
p - parity

Captured time can be converted to momentary power usage (kW) using formula
(3686400/(n_imp_per_kwh)/captured_time

71 bit packet contents:
1111 1010 | 1000 1101 | 1010 0001 | 0010 0001 | 1101 1111 /
xxxx xxxx | fccc dddd | dddd kkkk | kkkk kkkk | kkkk kkkk /
1100 0000 / 0000 0000 / 0001 0010 / 1101 111
kkkk kkkk | kkkk kkbo / pppp pppp / pppp ppp

x - constant
f - packet type (1 = 71 bit packet)
c - channel (LSbit first)
d - device ID (LSbit first)
k - impulse count since transmitter started (LSbit first)
b - low battery
o - overflow?
p - parity

Captured impulse count can be converted to energy usage (kWh) using formula
pulse_count/(n_imp_per_kwh)
*/

#include "decoder.h"

static uint8_t nibble_reverse[] = {
    0x0,0x8,0x4,0xC,0x2,0xA,0x6,0xE,0x1,0x9,0x5,0xD,0x3,0xB,0x7,0xF
};

static uint16_t checksum_taps[] = {
    0x4880, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x2080, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000
};

static uint8_t reverse(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static uint16_t next_mask(uint32_t mask) {
    uint16_t i;
    uint16_t next_mask;

    next_mask = mask>>1;
    for (i = 0; i < 15; i++) {
        if (mask&(1<<i)) {
            next_mask ^= checksum_taps[i];
        }
    }
    return next_mask;
}

static uint16_t calc_checksum(uint8_t *data, uint8_t datalen) {
    uint16_t i;
    uint32_t mask = 0x0001;
    uint16_t checksum = 0;

    for (i = datalen-1; i > 7; i--) {
        mask = next_mask(mask);
        if ((data[i/8]>>(7-i%8))&1)
          checksum ^= mask;
    }
    return checksum;
}


static int norgo_callback(r_device *decoder, bitbuffer_t *bitbuffer) {

    bitrow_t *bb = bitbuffer->bb;
    uint8_t *b = bb[0];

    uint16_t device_id = 0;
    uint8_t channel = 0;
    uint32_t impulse_gap = 0;
    uint64_t impulses = 0;
    uint8_t low_battery = 0;
    uint8_t bit;
    uint16_t checksum = 0;
    uint16_t calculated_checksum = 0;

    data_t *data;

    if ( bitbuffer->bits_per_row[0] != 56 && bitbuffer->bits_per_row[0] != 72 &&
         bitbuffer->bits_per_row[0] != 55 && bitbuffer->bits_per_row[0] != 71) {
         if (decoder->verbose)
            fprintf(stderr, "norgo_callback: wrong size of bit per row %d\n",
                    bitbuffer->bits_per_row[0] );
         return 0;
    }

    if ( b[0] != (uint8_t)~0xFA ) {
        if (decoder->verbose) {
             fprintf(stderr, "norgo_callback: wrong preamble\n");
             bitbuffer_print(bitbuffer);
        }
        return 0;
    }

    bitbuffer_invert(bitbuffer); /* inverted OOK_PULSE_DMC modulation */

    device_id = (nibble_reverse[(b[1]&0xF)]<<0) + (nibble_reverse[b[2]>>4]<<4);
    channel = (nibble_reverse[((b[1] >> 4) & 0x7)]>>1) + 1;
    if (0 == (b[1] & 0x80)) {
        calculated_checksum = calc_checksum(&b[0],5*8);
        checksum = (reverse(b[5])<<0) + ((uint16_t)reverse(b[6])<<8);
        if ( calculated_checksum != checksum ) {
            if (decoder->verbose) {
               fprintf(stderr, "norgo_callback: wrong checksum %02X vs. %02X\n",
                       calculated_checksum,checksum);
               bitbuffer_print(bitbuffer);
            }
            return 0;
        }

        impulse_gap = (nibble_reverse[b[2]&0xF]<<0) +
                      ((uint32_t)reverse(b[3])<<4) +
                      (((uint32_t)reverse(b[4])&0x7F)<<12);
        data = data_make(
              "brand",      "",              DATA_STRING, "Norgo Energy",
              "model",      "",              DATA_STRING, "NGE101",
              "id",         "Device ID",     DATA_INT, device_id,
              "channel",    "Channel",       DATA_INT, channel,
              "gap",        "Impulse gap",   DATA_INT, impulse_gap,
              NULL);
        decoder_output_data(decoder,data);

        return 1;
    }
    else {
        calculated_checksum = calc_checksum(&b[0],7*8);
        checksum = (reverse(b[7])<<0) + ((uint16_t)reverse(b[8])<<8);
        if ( calculated_checksum != checksum ) {
            if (decoder->verbose) {
                fprintf(stderr, "norgo_callback: wrong checksum %02X vs. %02X\n",
                        checksum,calculated_checksum);
                bitbuffer_print(bitbuffer);
             }
             return 0;
        }
        impulses =       (nibble_reverse[b[2]&0xF]<<0) +
                         ((uint64_t)reverse(b[3])<<4) +
                         ((uint64_t)reverse(b[4])<<12) +
                         ((uint64_t)reverse(b[5])<<20) +
                         (((uint64_t)reverse(b[6])&0x3F)<<28);
        low_battery = (b[6]&0x2) >> 1;

        /* Pulse count is totally 34 bits but we report only 32 bits (long int not
         * supported), which should be enough for the duration of battery */
        data = data_make(
                "brand",        "",             DATA_STRING, "Norgo Energy",
                "model",        "",             DATA_STRING, "NGE101",
                "id",           "Id",           DATA_INT, device_id,
                "channel",      "Channel",      DATA_INT, channel,
                "impulses",     "Impulses",     DATA_INT, (uint32_t) impulses,
                "battery",      "Battery",      DATA_STRING, !low_battery?"OK":"LOW",
                NULL);
        decoder_output_data(decoder,data);

        return 1;
    }
}

static char *output_fields[] = {
        "brand",
	"model",
	"id",
	"channel",
        "gap",
        "impulses",
        "battery",
	NULL
};

r_device norgo = {
   .name          = "Norgo NGE101",
   .modulation    = OOK_PULSE_DMC,
   .short_width   = 486,
   .long_width    = 972,
   .reset_limit   = 2100,
   .sync_width    = 0,
   .tolerance     = 120,
   .decode_fn     = &norgo_callback,
   .disabled      = 0,
   .fields        = output_fields
};
