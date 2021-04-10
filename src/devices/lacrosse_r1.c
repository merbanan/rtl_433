/** @file
    LaCrosse Technology View LTV-R1 Rainfall Gauge.

    Copyright (C) 2020 Mike Bruski (AJ9X) <michael.bruski@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
LaCrosse Technology View LTV-R1 Rainfall Gauge.

Note: This is an unfinished decoder.  It is able to read rainfall info
      from the sensor (raw_rain1 and raw_rain2) but does not calculate
      rain_mm from these values.  For now, only the raw values are
      output while work continues to calculate rain_mm.

Product pages:
https://www.lacrossetechnology.com/products/ltv-r1
https://www.lacrossetechnology.com/products/724-2310

Specifications:
- Rainfall 0 to 9999.9 mm

No internal inspection of the sensors was performed so can only
speculate that the remote sensors utilize a HopeRF CMT2119A ISM
transmitter chip which is tuned to 915Mhz.

No internal inspection of the console was performed but if the above
assumption is true, then the console most likely uses the HopeRF
CMT2219A ISM receiver chip.

(http://www.cmostek.com/download/CMT2119A_v0.95.pdf)
(http://www.cmostek.com/download/CMT2219A.pdf)
(http://www.cmostek.com/download/AN138%20CMT2219A%20Configuration%20Guideline.pdf)

Protocol Specification:

Data bits are NRZ encoded with logical 1 and 0 bits 104us in length.

LTV-R1
    PRE:32h SYN:32h ID:24h ?:4b SEQ:3d ?:1b RAIN:16d RAIN:16d CHK:8h END:32h

    CHK is CRC-8 poly 0x31 init 0x00 over 8 bytes following SYN

*/

#include "decoder.h"

static int lacrosse_r1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = { 0xd2, 0xaa, 0x2d, 0xd4 };

    data_t *data;
    uint8_t b[11];
    uint32_t id;
    int flags, seq, offset, chk;
    int raw_rain1, raw_rain2, msg_len;
    //float rain_mm;

    if (bitbuffer->num_rows > 1) {
        fprintf(stderr, "%s: Too many rows: %d\n", __func__, bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    msg_len = bitbuffer->bits_per_row[0];
    if (msg_len < 256) {
        if (decoder->verbose) {
            fprintf(stderr, "%s: Packet too short: %d bits\n", __func__, msg_len);
        }
        return DECODE_ABORT_LENGTH;
    } else if (msg_len > 272) {
        if (decoder->verbose) {
            fprintf(stderr, "%s: Packet too long: %d bits\n", __func__, msg_len);
        }
        return DECODE_ABORT_LENGTH;
    } else {
        if (decoder->verbose) {
           fprintf(stderr, "%s: packet length: %d\n", __func__, msg_len);
        }
    }

    offset = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= msg_len) {
        if (decoder->verbose) {
            fprintf(stderr, "%s: Sync word not found\n", __func__);
        }
        return DECODE_ABORT_EARLY;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 8 * 8);

    chk = crc8(b, 8, 0x31, 0x00);
    if (chk) {
        if (decoder->verbose) {
           fprintf(stderr, "%s: CRC failed!\n", __func__);
        }
        return DECODE_FAIL_MIC;
    }

    if (decoder->verbose) {
        bitbuffer_debug(bitbuffer);
    }

    id        = (b[0] << 16) | (b[1] << 8) | b[2];
    flags     = (b[3] & 0xf1); // masks off seq bits
    seq       = (b[3] & 0x0e) >> 1;
    raw_rain1 = b[4] << 8 | b[5];
    raw_rain2 = b[6] << 8 | b[7];

    // base and/or scale adjustments
    // how do we determine rain_mm from raw_rain1 and raw_rain2???
    //rain_mm   =  0.0;

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "LaCrosse-R1",
            "id",               "Sensor ID",        DATA_FORMAT, "%06x", DATA_INT, id,
            "seq",              "Sequence",         DATA_INT,     seq,
            "flags",            "unknown",          DATA_INT,     flags,
            "rain1",            "raw_rain1",        DATA_FORMAT, "%04x", DATA_INT, raw_rain1,
            "rain2",            "raw_rain2",        DATA_FORMAT, "%04x", DATA_INT, raw_rain2,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "seq",
        "flags",
        "rain1",
        "rain2",
        "mic",
        NULL,
};

// flex decoder m=FSK_PCM, s=104, l=104, r=9600
r_device lacrosse_r1 = {
        .name        = "LaCrosse Technology View LTV-R1 Rainfall Gauge",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 104,
        .long_width  = 104,
        .reset_limit = 9600,
        .decode_fn   = &lacrosse_r1_decode,
        .fields      = output_fields,
};
