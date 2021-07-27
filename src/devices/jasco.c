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

v0.1 based on the contact and water sensors Model 45131 / FCC ID QOB45131-3

v0.2 corrected decoder

v0.3 internal naming consistancies

*/

#include "decoder.h"

#define JASCO_MSG_BIT_LEN 86

static int jasco_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{

    if (bitbuffer->bits_per_row[0] != JASCO_MSG_BIT_LEN && bitbuffer->bits_per_row[0] != JASCO_MSG_BIT_LEN+1) {
        if (decoder->verbose > 1 && bitbuffer->bits_per_row[0] > 0) {
            fprintf(stderr, "%s: invalid bit count %d\n", __func__,
                     bitbuffer->bits_per_row[0]);
        }
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[4];
    uint8_t chk;
    uint32_t sensor_id = 0;
    int s_closed=0;
//    int battery=0;
    bitbuffer_t packet_bits;
    data_t *data;
    uint8_t const preamble[] = {0xfc, 0x0c};
    unsigned bitpos = 0;

    if (decoder->verbose > 1) {
        bitbuffer_debug(bitbuffer);
    }
    bitpos = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8;

    bitpos = bitbuffer_manchester_decode(bitbuffer, 0, bitpos, &packet_bits, 87);

    bitbuffer_extract_bytes(&packet_bits, 0, 0,b, 32);

    if (decoder->verbose > 1) {
        bitbuffer_debug(&packet_bits);
    }

    bitbuffer_clear(&packet_bits);

    chk = b[0] ^ b[1] ^ b[2];
    if (chk != b[3]) {
        return DECODE_FAIL_MIC;
    }


    sensor_id = (b[0] << 8)+ b[1];

    s_closed = ((b[2] & 0xef) == 0xef);


    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Jasco/GE Choice Alert Security Devices",
            "id",               "Id",           DATA_INT,    sensor_id,
            "status",           "Closed",       DATA_INT,    s_closed,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "status",
        "mic",
        NULL,
};

r_device jasco = {
        .name        = "Jasco/GE Choice Alert Security Devices",
        .modulation  = OOK_PULSE_PCM_RZ,
        .short_width = 250,
        .long_width  = 250,
        .reset_limit = 1800, // Maximum gap size before End Of Message
        .decode_fn   = &jasco_decode,
        .fields      = output_fields,

};
