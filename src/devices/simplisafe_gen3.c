/** @file
    SimpliSafe Gen 3 protocol.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
SimpliSafe Gen 3 protocol.

The data is sent at 433.9MHz using FSK at 4800 baud with a preamble and sync of `aaaaaaa 930b 51de`.

Data Layout:

    LEN:8h TYP:8h ID:32h CTR:24h CMAC:32h ENCR:80h CHK:16h

Example codes:

    55555554985a8ef0b01004fa89af407800c32b888bff61098d3627bdd5d369ca1800000000
    d55555552616a3bc2c04013ea26bd01e0030cae222ffd842634d89ef7574da728600000000
    d55555552616a3bc2c04013ea26bd21e0103b1a07f861673b5d1c531fa0bcd269c00000000
    55555554985a8ef0b01004fa89af4878040ec681fe1859ced74714c7e82f349a7000000000
    55555554985a8ef0b01004fa89af4878040ec681fe1859ced74714c7e82f349a7000000000

*/
static int simplisafe_gen3_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0x93, 0x0b, 0x51, 0xde}; // 32 bit

    int bitpos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 32) + 32;
    if (bitpos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }

    // a row needs to have at least 1+22+2 bytes
    if (bitpos + 25 * 8 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[25];
    bitbuffer_extract_bytes(bitbuffer, 0, bitpos, b, 25 * 8);

    // The row must start with len 22 (0x16) indicator
    if (b[0] != 0x16)
        return DECODE_ABORT_EARLY;

    // int len      = (b[0]);
    int msg_type = (b[1]);
    int id       = ((unsigned)b[2] << 24) | (b[3] << 16) | (b[4] << 8) | (b[5]);
    int ctr      = (b[6] << 16) | (b[7] << 8) | (b[8]);
    int cmac     = ((unsigned)b[9] << 24) | (b[10] << 16) | (b[11] << 8) | (b[12]);
    // int crc      = (b[23] << 8) | (b[24]);
    char encr[10 * 2 + 1]; // 13-22
    bitrow_snprint(&b[13], 10 * 8, encr, sizeof(encr));

    int chk = crc16(b, 25, 0x8005, 0xffff);
    if (chk) {
        if (decoder->verbose)
            bitrow_printf(b, 25 * 8, "%s: crc failed (%04x) ", __func__, chk);
        return DECODE_FAIL_MIC;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "SimpliSafe-Gen3",
            "id",               "ID",               DATA_FORMAT, "%08x", DATA_INT, id,
            "msg_type",         "Type",             DATA_FORMAT, "%02x", DATA_INT, msg_type,
            "ctr",              "Counter",          DATA_FORMAT, "%06x", DATA_INT, ctr,
            "cmac",             "CMAC",             DATA_FORMAT, "%08x", DATA_INT, cmac,
            "encr",             "Encrypted",        DATA_STRING, encr,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "msg_type",
        "ctr",
        "cmac",
        "encr",
        "mic",
        NULL,
};

r_device simplisafe_gen3 = {
        .name        = "SimpliSafe Gen 3 Home Security System",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 208, // 4800 baud
        .long_width  = 208,
        .reset_limit = 7000,
        .decode_fn   = &simplisafe_gen3_decode,
        .fields      = output_fields,
};
