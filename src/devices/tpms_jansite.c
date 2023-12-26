/** @file
    Jansite FSK 7 byte Manchester encoded checksummed TPMS data.

    Copyright (C) 2019 Andreas Spiess and Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Jansite Solar TPMS (Internal/External) Model TY02S.
- Working Temperature:-40 °C to 125 °C
- Working Frequency: 433.92MHz+-38KHz
- Tire monitoring range value: 0kPa-350kPa+-7kPa

Data layout (nibbles):

    II II II IS PP TT CC

- I: 28 bit ID
- S: 4 bit Status (deflation alarm, battery low etc)
- P: 8 bit Pressure (best guess quarter PSI, i.e. ~0.58 kPa)
- T: 8 bit Temperature (deg. C offset by 50)
- C: 8 bit Checksum
- The preamble is 0xaa..aa9 (or 0x55..556 depending on polarity)
*/

#include "decoder.h"

static int tpms_jansite_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    unsigned id;
    int flags;
    int pressure;
    int temperature;

    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 56);

    if (packet_bits.bits_per_row[0] < 56) {
        return DECODE_FAIL_SANITY;
        // decoder_logf(decoder, 3, __func__, "packet_bits.bits_per_row = %d", packet_bits.bits_per_row[0]);
    }
    b = packet_bits.bb[0];

    // TODO: validate checksum

    id          = (unsigned)b[0] << 20 | b[1] << 12 | b[2] << 4 | b[3] >> 4;
    flags       = b[3] & 0x0F;
    pressure    = b[4];
    temperature = b[5];
    //crc         = b[6];

    char id_str[7 + 1];
    snprintf(id_str, sizeof(id_str), "%07x", id);
    char code_str[7 * 2 + 1];
    snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6]); // figure out the checksum

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Jansite",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "flags",            "",             DATA_INT, flags,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure * 1.7,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature - 50.0,
            "code",             "",             DATA_STRING, code_str,
            //"mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_jansite_decode() */
static int tpms_jansite_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is
    // 0101 0101  0101 0101  0101 0101  0101 0110 = 55 55 55 56
    uint8_t const preamble_pattern[3] = {0xaa, 0xaa, 0xa9}; // after invert

    unsigned bitpos = 0;
    int ret         = 0;
    int events      = 0;

    bitbuffer_invert(bitbuffer);

    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 24)) + 80 <=
            bitbuffer->bits_per_row[0]) {
        ret = tpms_jansite_decode(decoder, bitbuffer, 0, bitpos + 24);
        if (ret > 0)
            events += ret;
        bitpos += 2;
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_kPa",
        "temperature_C",
        "code",
        //"mic",
        NULL,
};

r_device const tpms_jansite = {
        .name        = "Jansite TPMS Model TY02S",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,  // 12-13 samples @250k
        .long_width  = 52,  // FSK
        .reset_limit = 150, // Maximum gap size before End Of Message [us].
        .decode_fn   = &tpms_jansite_callback,
        .disabled    = 1, // Unknown checksum
        .fields      = output_fields,
};
