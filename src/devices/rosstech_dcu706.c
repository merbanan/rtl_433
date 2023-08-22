/** @file
    Rosstech Digital Control Unit DCU-706/Sundance

    Copyright (C) 2023 suaveolent

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Rosstech Digital Control Unit DCU-706/Sundance

...supported models etc.

Data layout:

    IIII F TTT HH CC

- I: 16 bit ID
- F: 4 bit flags
- T: 12 bit temperature, scale 10
- H: 8 bit humidity
- C: 8 bit CRC-8, poly 0x81

Format string:

    ID:16h FLAGS:4h TEMP:12h HUMI:8h CRC:8h

...Decoding notes like endianness, signedness

*/
static int rosstech_dcu706_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    printf("Called!!!!!");
    decoder_log(decoder, 1, __func__, "CRC error.");
    return 1;


    // uint8_t const preamble[] = {0x55, 0x2D, 0xD4};

    // if (bitbuffer->num_rows != 1) {
    //     return DECODE_ABORT_EARLY;
    // }

    // unsigned bits = bitbuffer->bits_per_row[0];

    // if (bits < 136 ) {                 // too small
    //     return DECODE_ABORT_LENGTH;
    // }

    // unsigned pos = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof (preamble) * 8);

    // if (pos >= bits) {
    //     return DECODE_ABORT_EARLY;
    // }

    // data_t *data;
    // uint8_t b[14];

    // pos += sizeof(preamble) * 8;
    // bitbuffer_extract_bytes(bitbuffer, 0, pos, b, sizeof(b) * 8);

    // if (b[0] != 0 && b[6] != 1) {
    //     return DECODE_ABORT_EARLY;
    // }

    // uint16_t crc_calc = crc16(b, 14, 0x1021, 0x0000);

    // if (crc_calc != 0 ) {
    //     decoder_log(decoder, 1, __func__, "CRC error.");
    //     return DECODE_FAIL_MIC;
    // }

    // char msg0[20], msg1[20];
    // sprintf(msg0, "%02x%02x%02x%02x%02x", b[1], b[2], b[3], b[4], b[5]);
    // sprintf(msg1, "%02x%02x%02x%02x%02x", b[7], b[8], b[9], b[10], b[11]);

    // /* clang-format off */
    // data = data_make(
    //     "model",   "Model",     DATA_STRING,   "Chamberlain-CWPIRC",
    //     "msg_0",   "Message 0", DATA_STRING,    msg0,
    //     "msg_1",   "Message 1", DATA_STRING,    msg1,
    //     "mic",     "Integrity", DATA_STRING,   "CRC",
    //     NULL);
    // /* clang-format on */

    // decoder_output_data(decoder, data);
    // return 1;

}

static char const *const output_fields[] = {
        "model",
        "msg_0",
        "msg_1",
        "mic",
        NULL,
};

r_device const rosstech_dcu706 = {
        .name        = "Rosstech Digital Control Unit DCU-706/Sundance",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 204,
        .long_width  = 204,
        .sync_width  = 0, // 1:10, tuned to widely match 2450 to 2850
        .reset_limit = 208896,
        .decode_fn   = &rosstech_dcu706_decode,
        .fields      = output_fields,
};