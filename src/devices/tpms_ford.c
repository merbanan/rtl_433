/** @file
    FSK 8 byte Manchester encoded TPMS with simple checksum.

    Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
FSK 8 byte Manchester encoded TPMS with simple checksum.
Seen on Ford Fiesta, Focus, Kuga, Escape ...

Seen on 433.92 MHz.
Likely VDO-Sensors, Type "S180084730Z", built by "Continental Automotive GmbH".

Packet nibbles:

    II II II II PP TT EF CC

- I = ID
- P = Pressure, maybe PSI scale 0.25?
- T = Temperature, in C, offset about 56
- E = high nibble flags ?tp?, t: temperature related bit, p: 9th pressure bit
- F = Flags, (46: 87% 1e: 5% 06: 2% 4b: 1% 66: 1% 0e: 1% 44: 1%)
- C = Checksum, SUM bytes 0 to 6 = byte 7
*/

#include "decoder.h"

static int tpms_ford_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    data_t *data;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    unsigned id;
    char id_str[9];
    int code;
    char code_str[7];
    float pressure_psi;
    int temperature_c;
    int psibits;

    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 160);

    // require 64 data bits
    if (packet_bits.bits_per_row[0] < 64) {
        return 0;
    }
    b = packet_bits.bb[0];

    if (((b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6]) & 0xff) != b[7]) {
        return 0;
    }

    id = (unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    sprintf(id_str, "%08x", id);

    code = b[4] << 16 | b[5] << 8 | b[6];
    sprintf(code_str, "%06x", code);

    /* range seems to have different formulas */
    psibits = (((b[6] & 0x20) << 3) | b[4]);
    if (psibits < 90)
        pressure_psi = 0.3 + psibits * 0.25f; // BdyCM + FORScan
    else
        pressure_psi = 6.8 + psibits * 0.2122727273;
    temperature_c = b[5] - 56; // approximate
    if (b[6] & 0x40)           // temperature scale mode?
        temperature_c = (b[5] ^ 0x80) - 56;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Ford",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "pressure_PSI",     "Pressure",     DATA_FORMAT, "%.2f PSI", DATA_DOUBLE, pressure_psi,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C",   DATA_DOUBLE, (float)temperature_c,
            "code",             "",             DATA_STRING, code_str,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_ford_decode() */
static int tpms_ford_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is 55 55 55 56 (inverted: aa aa aa a9)
    uint8_t const preamble_pattern[2] = {0xaa, 0xa9}; // 16 bits

    int row;
    unsigned bitpos;
    int ret    = 0;
    int events = 0;

    bitbuffer_invert(bitbuffer);

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                preamble_pattern, 16)) + 144 <=
                bitbuffer->bits_per_row[row]) {
            ret = tpms_ford_decode(decoder, bitbuffer, row, bitpos + 16);
            if (ret > 0)
                events += ret;
            bitpos += 15;
        }
    }

    return events > 0 ? events : ret;
}

static char *output_fields[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_PSI",
        "temperature_C",
        "code",
        "mic",
        NULL,
};

r_device tpms_ford = {
        .name        = "Ford TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,  // 12-13 samples @250k
        .long_width  = 52,  // FSK
        .reset_limit = 150, // Maximum gap size before End Of Message [us].
        .decode_fn   = &tpms_ford_callback,
        .fields      = output_fields,
};
