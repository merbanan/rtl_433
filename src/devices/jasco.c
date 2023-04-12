/** @file
    Jasco/GE Choice Alert Wireless Device Decoder.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Jasco/GE Choice Alert Wireless Device Decoder.

- Frequency: 318.01 MHz

Manchester PCM with a de-sync preamble of 0xFC0C (11111100000011000).

Packets are 32 bit, 24 bit data and 8 bit XOR checksum.

*/

#include "decoder.h"

static int jasco_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xfc, 0x0c}; // length 16

    if (bitbuffer->bits_per_row[0] < 80
            || bitbuffer->bits_per_row[0] > 87) {
        if (bitbuffer->bits_per_row[0] > 0) {
            decoder_logf(decoder, 2, __func__, "invalid bit count %d", bitbuffer->bits_per_row[0]);
        }
        return DECODE_ABORT_EARLY;
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0, preamble, 16) + 16;

    if (start_pos + 64 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_t packet_bits = {0};
    bitbuffer_manchester_decode(bitbuffer, 0, start_pos, &packet_bits, 32);

    if (packet_bits.bits_per_row[0] < 32) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = packet_bits.bb[0];

    int chk = b[0] ^ b[1] ^ b[2] ^ b[3];
    if (chk) {
        return DECODE_FAIL_MIC;
    }

    int sensor_id = (b[0] << 8) | b[1];

    int s_closed = ((b[2] & 0xef) == 0xef);
    // int battery = 0;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Jasco-Security",
            "id",               "Id",           DATA_INT,    sensor_id,
            "status",           "Closed",       DATA_INT,    s_closed,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "status",
        "mic",
        NULL,
};

r_device const jasco = {
        .name        = "Jasco/GE Choice Alert Security Devices",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 250,
        .long_width  = 250,
        .reset_limit = 1800, // Maximum gap size before End Of Message
        .decode_fn   = &jasco_decode,
        .fields      = output_fields,

};
