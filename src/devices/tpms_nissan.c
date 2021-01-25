/** @file
    Nissan FSK 37 bit Manchester encoded checksummed TPMS data.
    Reference issue: https://github.com/merbanan/rtl_433/issues/1024

    Copyright (C) 2021 Alex Wilson <alex.david.wilson@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Nissan FSK 37 bit Manchester encoded checksummed TPMS data

MODE:2d TPMS_ID:20h UNKNOWN:5b (PSI+THREE)*FOUR=8d UNKNOWN:2b

*/

#include "decoder.h"

static int tpms_nissan_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    data_t *data;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    int mode;
    // char mode_str[3];
    unsigned id;
    char id_str[6];
    int unknown0;
    int pressure_psi;
    int unknown1;

    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 113);
    bitbuffer_invert(&packet_bits);  // Manchester (G.E. Thomas) Decoded

    // FIXME Debug stuff
    // fprintf(stderr, "packet_bits:\n");
    // bitbuffer_print(&packet_bits);

    // fprintf(stderr, "%s : bits %d\n", __func__, packet_bits.bits_per_row[0]);
    if ( packet_bits.bits_per_row[0] < 37) {
        return DECODE_FAIL_SANITY; // sanity check failed
    }

    b = packet_bits.bb[0];

    // TODO Is there any parity or other checks we can preform to return
    // DECODE_ABORT_EARLY or DECODE_FAIL_MIC

    mode = b[0] >> 6;
    //sprintf(mode_str, "%2d", mode);

    id = (unsigned)((b[0] & 0x3F) << 14) | (b[1] << 6) | (b[2] >> 2);
    sprintf(id_str, "%05x", id);

    unknown0 = ((b[2] & 0x3) << 3) | (b[3] >> 5);
    pressure_psi = ((b[3] & 0x1F) << 3) | (b[4] >> 5);
    unknown1 = (b[4] & 0x1F) >> 3;

    data = data_make(
        "model",           "",     DATA_STRING, "Nissan",
        "type",            "",     DATA_STRING, "TPMS",
        "mode",            "",     DATA_INT, mode,
        "id",              "",     DATA_STRING, id_str,
        "unknown0",        "",     DATA_INT, unknown0,
        "pressure_psi", "Pressure", DATA_FORMAT, "%.1f PSI", DATA_DOUBLE, (double)(pressure_psi / 4.0) - 3.0,
        "unknown1",        "",     DATA_INT, unknown1,
        NULL);

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_nissan_decode() */
static int tpms_nissan_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // preamble is f5 55 55 55 e
    uint8_t const preamble_pattern[5] = {0xf5, 0x55, 0x55, 0x55, 0xe0}; // 36 bits

    unsigned bitpos = 0;
    int ret         = 0;
    int events      = 0;

    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 36)) + 77 <=
            bitbuffer->bits_per_row[0]) {
        ret = tpms_nissan_decode(decoder, bitbuffer, 0, bitpos + 36);
        if (ret > 0)
            events += ret;
        bitpos += 1;
    }

    return events > 0 ? events : ret;
}

static char *output_fields[] = {
    "model",
    "type",
    "mode",
    "id",
    "unknown0",
    "pressure_psi",
    "unknown1",
    NULL,
};

r_device tpms_nissan = {
    .name           = "Nissan TPMS",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 120, // TODO The preamble plus pre-MC data is 113, what should this be?
    .long_width     = 120, // FSK
    .reset_limit    = 250, // Maximum gap size before End Of Message [us]. TODO What should this be?
    .decode_fn      = &tpms_nissan_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
