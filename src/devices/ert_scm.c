/** @file
    ERT Standard Consumption Message (SCM) sensors.

    Copyright (C) 2020 Benjamin Larsson.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ERT Standard Consumption Message (SCM) sensors.

Random information:

https://github.com/bemasher/rtlamr

https://en.wikipedia.org/wiki/Encoder_receiver_transmitter

https://patentimages.storage.googleapis.com/df/23/d3/f0c33d9b2543ff/WO2007030826A2.pdf

96-bit Itron® Standard Consumption Message protocol
https://www.smartmetereducationnetwork.com/uploads/how-to-tell-if-I-have-a-ami-dte-smart-advanced-meter/Itron%20Centron%20Meter%20Technical%20Guide1482163-201106090057150.pdf (page 28)

Data layout:

    SAAA AAAA  AAAA AAAA
    AAAA AiiR  PPTT TTEE
    CCCC CCCC  CCCC CCCC
    CCCC CCCC  IIII IIII
    IIII IIII  IIII IIII
    XXXX XXXX  XXXX XXXX

- S - Sync bit
- A - Preamble
- i - ERT ID Most Significant bits
- R - Reserved
- P - Physical tamper
- T - ERT Type (4 and 7 are mentioned in the pdf)
- E - Encoder Tamper
- C - Consumption data
- I - ERT ID Least Significant bits
- X - CRC (polynomial 0x6F63)

https://web.archive.org/web/20090828043201/http://www.openamr.org/wiki/ItronERTModel45

*/

static int ert_scm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // TODO: Verify preamble
    unsigned sync_index;
    static const uint8_t ert_frame_sync[]  = {0x1F, 0x2A, 0x60};
    static const uint8_t ert_frame_sync_alternative []  = {0x01, 0x53, 0x00};

    uint8_t b[12];
    uint8_t physical_tamper, ert_type, encoder_tamper;
    uint32_t consumption_data, ert_id;
    data_t *data;

    if (bitbuffer->bits_per_row[0] < 96) {
        return DECODE_ABORT_LENGTH;
    }

    sync_index = bitbuffer_search(bitbuffer, 0, 0, ert_frame_sync, 21);

    if (sync_index >= bitbuffer->bits_per_row[0]) {
        sync_index = bitbuffer_search(bitbuffer, 0, 0, ert_frame_sync_alternative, 21);

        if(sync_index >= bitbuffer->bits_per_row[0]){
            if (decoder->verbose > 1) {
                 fprintf(stderr, "%s: DECODE_ABORT_EARLY %d sync_index %d\n ", __func__, bitbuffer->bits_per_row[0], sync_index);
                 bitbuffer_print(bitbuffer);
                 decoder_output_bitbufferf(decoder, bitbuffer, "%s: ", __func__);
            }

            return DECODE_ABORT_EARLY;
        }
    }


    if ( (bitbuffer->bits_per_row[0] - sync_index) < 96) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: DECODE_ABORT_LENGTH %d\n ", __func__, bitbuffer->bits_per_row[0]);
        }

        return DECODE_ABORT_LENGTH;
    }


    bitbuffer_extract_bytes(bitbuffer, 0, sync_index, b, 12 * 8);

    if (crc16(&b[2], 10, 0x6F63, 0)){
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: DECODE_FAIL_MIC 0x00\n", __func__);
        }

        return DECODE_FAIL_MIC;
    }


    /* Instead of detecting the preamble we rely on the
     * CRC and extract the parameters from the back */

    /* Extract parameters */
    physical_tamper = (b[3]&0xC0) >> 6;
    ert_type = (b[3]>>2) & 0x0F;
    encoder_tamper = b[3]&0x03;
    consumption_data = (b[4]<<16) | (b[5]<<8) | b[6];
    ert_id = ((b[2]&0x06)<<23) | (b[7]<<16) | (b[8]<<8) | b[9];

    /* clang-format off */
    data = data_make(
            "model",           "",                 DATA_STRING, "ERT-SCM",
            "id",              "Id",               DATA_INT,    ert_id,
            "physical_tamper", "Physical Tamper",  DATA_INT, physical_tamper,
            "type",        "ERT Type",         DATA_INT, ert_type,
            "encoder_tamper",  "Encoder Tamper",   DATA_INT, encoder_tamper,
            "consumption_data","Consumption Data", DATA_INT, consumption_data,
            "mic",             "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "physical_tamper",
        "type",
        "encoder_tamper",
        "consumption_data",
        "mic",
        NULL,
};

r_device ert_scm = {
        .name        = "ERT Standard Consumption Message (SCM)",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 30,
        .long_width  = 30,
        .gap_limit   = 0,
        .reset_limit = 80, //64,
        .decode_fn   = &ert_scm_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
