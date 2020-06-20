/** @file
    Norgo Energy NGE101 decoder.

    Copyright (C) 2019 jamaron

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int norgo_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Norgo Energy NGE101 decoder.

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
    ssss ssss | fccc dddd | dddd tttt | tttt tttt | tttt tttu /
    1010 1101 / 1010 000?
    xxxx xxxx / pppp ppp?

- s: sync byte, 0xfa
- f: packet type (0 = 55 bit packet)
- c: channel (LSbit first)
- d: device ID (LSbit first)
- t: time in 1/1024 seconds between the last two impulses (LSbit first)
- u: unknown
- x: xor sum (starting at byte 1)
- p: parity

Captured time can be converted to momentary power usage (kW) using formula:
(3686400/(n_imp_per_kwh)/captured_time

71 bit packet contents:

    1111 1010 | 1000 1101 | 1010 0001 | 0010 0001 | 1101 1111 /
    ssss ssss | fccc dddd | dddd kkkk | kkkk kkkk | kkkk kkkk /
    1100 0000 / 0000 0000 / 0001 0010 / 1101 111?
    kkkk kkkk | kkkk kkbo / xxxx xxxx / pppp ppp?

- s: sync byte, 0xfa
- f: packet type (1 = 71 bit packet)
- c: channel (LSbit first)
- d: device ID (LSbit first)
- k: impulse count since transmitter started (LSbit first)
- b: low battery
- o: overflow?
- x: xor sum (starting at byte 1)
- p: parity

Captured impulse count can be converted to energy usage (kWh) using formula:
pulse_count/(n_imp_per_kwh)
*/

#include "decoder.h"

static uint16_t checksum_taps[] = {
        0x4880, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x2080, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000,
};

static uint16_t next_mask(uint32_t mask)
{
    uint16_t i;
    uint16_t next_mask;

    next_mask = mask >> 1;
    for (i = 0; i < 15; i++) {
        if (mask & (1 << i)) {
            next_mask ^= checksum_taps[i];
        }
    }
    return next_mask;
}

static uint8_t calc_checksum(uint8_t *data, uint8_t datalen)
{
    uint16_t i;
    uint32_t mask = 0x0001;
    uint16_t chks = 0;

    for (i = datalen - 1; i > 7; i--) {
        mask = next_mask(mask);
        if ((data[i / 8] >> (i % 8)) & 1)
            chks ^= mask;
    }
    return chks >> 8;
}

static int norgo_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[0];

    int device_id;
    int channel;
    int impulse_gap;
    uint64_t impulses;
    int low_battery;
    //int maybe_overflow;
    int checksum;
    int calc_chk;

    if (bitbuffer->bits_per_row[0] != 56
            && bitbuffer->bits_per_row[0] != 72
            && bitbuffer->bits_per_row[0] != 55
            && bitbuffer->bits_per_row[0] != 71) {
        if (decoder->verbose)
            fprintf(stderr, "%s: wrong size of bit per row %d\n",
                    __func__, bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH;
    }

    if (b[0] != (uint8_t)~0xFA) {
        if (decoder->verbose)
            bitbuffer_printf(bitbuffer, "%s: wrong preamble: ", __func__);
        return DECODE_ABORT_EARLY;
    }

    int xor = xor_bytes(b + 1, (bitbuffer->bits_per_row[0] - 15) / 8);
    if (xor != 0xff) { // before invert 0 is ff
        if (decoder->verbose)
            bitrow_printf(b, bitbuffer->bits_per_row[0], "%s: XOR fail (%02x): ",
                    __func__, xor);
        return DECODE_FAIL_MIC;
    }

    bitbuffer_invert(bitbuffer); // inverted OOK_PULSE_DMC modulation
    reflect_bytes(b, (bitbuffer->bits_per_row[0] + 1) / 8);

    device_id = ((b[1] & 0xF0) >> 4) | ((b[2] & 0x0f) << 4);
    channel   = ((b[1] & 0x0e) >> 1) + 1;
    if (0 == (b[1] & 0x1)) {
        calc_chk = calc_checksum(b, 5 * 8);
        checksum = b[6];
        if (calc_chk != checksum) {
            if (decoder->verbose)
                bitbuffer_printf(bitbuffer, "%s: wrong checksum %02X vs. %02X: ",
                        __func__, calc_chk, checksum);
            return DECODE_FAIL_MIC;
        }

        impulse_gap = (b[2] >> 4) | (b[3] << 4) | ((b[4] & 0x7F) << 12);
        /* clang-format off */
        data = data_make(
                "model",        "",             DATA_STRING, "Norgo-NGE101",
                "id",           "Device ID",    DATA_INT,    device_id,
                "channel",      "Channel",      DATA_INT,    channel,
                "gap",          "Impulse gap",  DATA_INT,    impulse_gap,
                "mic",          "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    else {
        calc_chk = calc_checksum(b, 7 * 8);
        checksum = b[8];
        if (calc_chk != checksum) {
            if (decoder->verbose)
                bitbuffer_printf(bitbuffer, "%s: wrong checksum %02X vs. %02X: ",
                        __func__, checksum, calc_chk);
            return DECODE_FAIL_MIC;
        }
        impulses = (b[2] >> 4) | (b[3] << 4) | (b[4] << 12) | (b[5] << 20) | (((uint64_t)b[6] & 0x3F) << 28);

        low_battery    = (b[6] & 0x40) >> 6;
        //maybe_overflow = (b[6] & 0x80) >> 7;

        // Pulse count is totally 34 bits but we report only 32 bits,
        // should be enough for the duration of battery.

        /* clang-format off */
        data = data_make(
                "model",        "",             DATA_STRING, "Norgo-NGE101",
                "id",           "Id",           DATA_INT,    device_id,
                "channel",      "Channel",      DATA_INT,    channel,
                "impulses",     "Impulses",     DATA_INT,    (uint32_t)impulses,
                "battery_ok",   "Battery",      DATA_INT,    !low_battery,
                "mic",          "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "gap",
        "impulses",
        "battery_ok",
        NULL,
};

r_device norgo = {
        .name        = "Norgo NGE101",
        .modulation  = OOK_PULSE_DMC,
        .short_width = 486,
        .long_width  = 972,
        .reset_limit = 2100,
        .sync_width  = 0,
        .tolerance   = 120,
        .decode_fn   = &norgo_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
