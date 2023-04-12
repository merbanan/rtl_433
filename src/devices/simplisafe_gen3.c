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

Known message length/types:
- Arm: 15 01
- Disarm: 18 01
- Sensors: 16 02

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

    // a row needs to have at least 1+21+2 bytes
    if (bitpos + 24 * 8 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[27]; // for length 21 to 24 (plus 3)
    bitbuffer_extract_bytes(bitbuffer, 0, bitpos, b, 27 * 8);

    // The row must start with length indicator of 21, 22, or 24 (0x15, 0x16, 0x18)
    if (b[0] != 0x15 && b[0] != 0x16 && b[0] != 0x18)
        return DECODE_ABORT_EARLY;

    int len      = (b[0]); // verified to be 21, 22, or 24
    int msg_type = (b[1]);
    int id       = ((unsigned)b[2] << 24) | (b[3] << 16) | (b[4] << 8) | (b[5]);
    int ctr      = (b[8] << 16) | (b[7] << 8) | (b[6]); // note: little endian
    int cmac     = ((unsigned)b[9] << 24) | (b[10] << 16) | (b[11] << 8) | (b[12]);
    // int crc      = (b[23] << 8) | (b[24]);
    char encr[12 * 2 + 1]; // 9, 10, or 12 hex bytes
    bitrow_snprint(&b[13], (len - 12) * 8, encr, sizeof(encr));

    int chk = crc16(b, len + 3, 0x8005, 0xffff);
    if (chk) {
        decoder_logf_bitrow(decoder, 1, __func__, b, (len + 3) * 8, "crc failed (%04x)", chk);
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

static char const *const output_fields[] = {
        "model",
        "id",
        "msg_type",
        "ctr",
        "cmac",
        "encr",
        "mic",
        NULL,
};

r_device const simplisafe_gen3 = {
        .name        = "SimpliSafe Gen 3 Home Security System",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 208, // 4800 baud
        .long_width  = 208,
        .reset_limit = 7000,
        .decode_fn   = &simplisafe_gen3_decode,
        .fields      = output_fields,
};
