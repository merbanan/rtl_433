/** @file
    Flowis water meter.

    Copyright (C) 2023 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Heavily based on marlec_solar.c
    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>
*/

/**
Flowis water meter.

There are several different message types with different message lengths.
All signals are transmitted with a preamble (0xA or 0x5) and then the
syncword d391 d391. This is a popular syncword used by the CC110x transceiver
family.


Message layout type 1 (0x15 bytes of length), 4-bit nibble:

               0  1  2 3 4 5  6  7 8 9 A  B  C  ....... 0x15
    SSSS SSSS LL YY IIIIIIII ?? TTTTTTTT AA BB ???????? CC

- S 32b: 2 x 16 bit sync words d391 d391 (32 bits)
- L  8b: message length, different message types have different lengths
- Y  8b: message type (1 and 2 has been observed
- I 32b: meter id, visible on the actual meter
- ?  8b: unknown
- T 32b: timestamp, bitpacked
- V 32b: Volume in m3
- A  8b: Alarm
- B  8b: Backflow
- ?  xb: unknown
- C 16b: CRC-16 with poly=0x8005 and init=0xFFFF over data after sync

Message type 2 uses same message syntax, length type, payload and checksum.

Type 2 messages usually contain long runs of zeros that might cause bitstream desyncs.

*/

#include "decoder.h"

static int flowis_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {
            /*0xaa, 0xaa, */ 0xaa, 0xaa, // preamble
            0xd3, 0x91, 0xd3, 0x91       // sync word
    };

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int row = 0;
    // Validate message and reject it as fast as possible : check for preamble
    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, sizeof (preamble) * 8);

    if (start_pos == bitbuffer->bits_per_row[row]) {
        return DECODE_ABORT_EARLY; // no preamble detected
    }

    uint8_t len;
    bitbuffer_extract_bytes(bitbuffer, row, start_pos + sizeof (preamble) * 8, &len, 8);


    uint8_t frame[256+2+1] = {0}; // uint8_t max bytes + 2 bytes crc + 1 length byte
    frame[0] = len;
    // Get frame (len don't include the length byte and the crc16 bytes)
    bitbuffer_extract_bytes(bitbuffer, row,
            start_pos + (sizeof (preamble) + 1) * 8,
            &frame[1], (len + 2) * 8);

    decoder_log_bitrow(decoder, 2, __func__, frame, (len + 1) * 8, "frame data");

    uint16_t crc = crc16(frame, len + 1, 0x8005, 0xffff);

    if ((frame[len + 1] << 8 | frame[len + 2]) != crc) {
        decoder_logf(decoder, 1, __func__, "CRC invalid %04x != %04x",
                frame[len + 1] << 8 | frame[len + 2], crc);
        return DECODE_FAIL_MIC;
    }
    uint8_t* b = frame;
    int type = b[1];

    /* Only type 1 decoding is supported */
    if (type != 1) return DECODE_ABORT_EARLY;

    int id   = b[5] << 24 | b[4] << 16 | b[3] << 8 | b[2];
    int volume = b[13] << 16 | b[12] << 8 | b[11];

    int fts_year = b[10] >> 2;
    int fts_mth  = ((b[9]>>6) | (b[10]&3)<<2);
    int fts_day  = (b[9]&0x3E) >> 1;
    int fts_hour = (b[8]>>4) | ((b[9]&1)<<4);
    int fts_min  = ((b[8]&0xF)<<2) | ((b[7]&0xC0)>>6);
    int fts_sec  = b[7]&0x3F;
    char fts_str[20];
    snprintf(fts_str, sizeof(fts_str), "%4d-%02d-%02dT%02d:%02d:%02d", fts_year + 2000, fts_mth, fts_day, fts_hour, fts_min, fts_sec);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "Flowis",
            "id",           "Meter id",     DATA_INT,    id,
            "type",         "Type",         DATA_INT,    type,
            "volume_m3",    "Volume",       DATA_FORMAT, "%.3f m3", DATA_DOUBLE, volume/1000.0,
            "device_time",  "Device time",  DATA_STRING, fts_str,
            "alarm",        "Alarm",        DATA_INT,    b[15],
            "backflow",     "Backflow",     DATA_INT,    b[14],
            "mic",          "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "type",
        "volume_m3",
        "device_time",
        "alarm",
        "backflow",
        "mic",
        NULL,
};

r_device const flowis = {
        .name        = "Flowis flow meters",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 10,
        .long_width  = 10,
        .reset_limit = 5000,
        .decode_fn   = &flowis_decode,
        .fields      = output_fields,
};
