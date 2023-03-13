/** @file
    SmartFire Proflame 2 remote protocol.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>
    based on protocol decode Copyright (C) 2020 johnellinwood

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/** @fn int proflame2_decode(r_device *decoder, bitbuffer_t *bitbuffer)
SmartFire Proflame 2 remote protocol.

See https://github.com/johnellinwood/smartfire

The command bursts are transmitted at 314,973 KHz using On-Off Keying (OOK).
Transmission rate is 2400 baud. Packet is transmitted 5 times, repetitions are separated by 12 low amplitude bits (zeros).

Encoded with a variant of Thomas Manchester encoding:
0 is represented by 01, a 1 by 10, zero padding (Z) by 00, and synchronization words (S) as 11.
The encoded command packet is 182 bits, and the decoded packet is 91 bits.

A packet is made up of 7 words, each 13 bits,
starts with a synchronization symbol, followed by a 1 as a guard bit,
then 8 bits of data, a padding bit, a parity bit, and finally a 1 as an end guard bit.
The padding bit is 1 for the first word and 0 for all other words.
The parity bit is calculated over the data bits and the padding bit,
and is 0 if there are an even number of ones and 1 if there are an odd number of ones.

The payload data is 7 bytes:

- Serial 1
- Serial 2
- Serial 3
- Command 1
- Command 2
- Error Detection 1
- Error Detection 2

*/
#include "decoder.h"

/// out needs to be at least (bits / 26, usually 7) bytes long
static int proflame2_mc(bitbuffer_t *bitbuffer, unsigned row, unsigned start, uint8_t *out)
{
    uint8_t *b   = bitbuffer->bb[row];
    unsigned pos = start;
    for (int f = 0;; ++f) {
        if (bitbuffer->bits_per_row[row] - pos < 26)
            return f;
        // expect sync and start bit of "1110"
        int sync = bitrow_get_bit(b, pos + 0) << 3
                | bitrow_get_bit(b, pos + 1) << 2
                | bitrow_get_bit(b, pos + 2) << 1
                | bitrow_get_bit(b, pos + 3) << 0;
        pos += 4;
        if (sync != 0xe)
            return f;

        bitbuffer_t decoded = {0};
        pos = bitbuffer_manchester_decode(bitbuffer, row, pos, &decoded, 11);
        if (decoded.bits_per_row[0] != 11)
            return f;

        // invert IEEE MC to G.E.T. MC
        uint8_t data = decoded.bb[0][0] ^ 0xff;
        uint8_t flag = decoded.bb[0][1] ^ 0xe0;

        int pad = (flag >> 7) & 1;
        int par = (flag >> 6) & 1;
        int end = (flag >> 5) & 1;

        if (pad != (f == 0))
            return f;

        int par_chk = parity8(data) ^ pad ^ par;
        if (par_chk)
            return f;

        if (end != 1)
            return f;

        out[f] = data;
    }
    return 0;
}

static int proflame2_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        uint8_t b[7] = {0};
        int ret = proflame2_mc(bitbuffer, row, 0, b);

        if (ret != 7)
            continue;

        int id   = b[0] << 16 | b[1] << 8 | b[2];
        int cmd1 = b[3];
        int cmd2 = b[4];
        int err1 = b[5];
        int err2 = b[6];

        int pilot      = (b[3] >> 7);
        int light      = (b[3] & 0x70) >> 4;
        int thermostat = (b[3] & 0x02) >> 1;
        int power      = (b[3] & 0x01);
        int front      = (b[4] >> 7);
        int fan        = (b[4] & 0x70) >> 4;
        int aux        = (b[4] & 0x08) >> 3;
        int flame      = (b[4] & 0x07);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, "Proflame2-Remote",
                "id",           "Id",           DATA_FORMAT, "%06x", DATA_INT,    id,
                "cmd1",         "Cmd1",         DATA_FORMAT, "%02x", DATA_INT,    cmd1, // add chk then remove this
                "cmd2",         "Cmd2",         DATA_FORMAT, "%02x", DATA_INT,    cmd2, // add chk then remove this
                "err1",         "Err1",         DATA_FORMAT, "%02x", DATA_INT,    err1, // add chk then remove this
                "err2",         "Err2",         DATA_FORMAT, "%02x", DATA_INT,    err2, // add chk then remove this
                "pilot",        "Pilot",        DATA_INT,    pilot,
                "light",        "Light",        DATA_INT,    light,
                "thermostat",   "Thermostat",   DATA_INT,    thermostat,
                "power",        "Power",        DATA_INT,    power,
                "front",        "Front",        DATA_INT,    front,
                "fan",          "Fan",          DATA_INT,    fan,
                "aux",          "Aux",          DATA_INT,    aux,
                "flame",        "Flame",        DATA_INT,    flame,
                "mic",          "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return 0;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "pilot",
        "light",
        "thermostat",
        "power",
        "front",
        "fan",
        "aux",
        "flame",
        "mic",
        NULL,
};

r_device const proflame2 = {
        .name        = "SmartFire Proflame 2 remote control",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 417, // 2400 baud
        .long_width  = 417,
        .gap_limit   = 1000, // 12 low amplitudes are 5000 us
        .reset_limit = 6000,
        .decode_fn   = &proflame2_decode,
        .fields      = output_fields,
};
