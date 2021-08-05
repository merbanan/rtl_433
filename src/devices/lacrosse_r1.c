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

LTV-R1:

    PRE:32h SYNC:32h ID:24h ?:4b SEQ:3d ?:1b RAIN:24h CRC:8h CHK?:8h TRAILER:96h

    CHK is CRC-8 poly 0x31 init 0x00 over 7 bytes following SYNC

    {164} 380322  0e  00aa14  6a  93  00...
    {164} 380322  00  00aa1a  60  81  00...
    {162} 380322  06  00aa26  d1  04  00...

LTV-R3:
does not have the CRC at byte 8 but a second 24 bit value and the check at byte 11.

    PRE:32h SYNC:32h ID:24h ?:4b SEQ:3d ?:1b RAIN:24h CRC:8h TRAILER:56h

    {145} 70f6a2 00 015402 015401  ae  00...
    {142} 70f6a0 88 015400 015400  24  00...
    {143} 70f6a2 46 00a800 015401  e2  00...
    {144} 70f6a2 48 00aa02 00aa00  3d  00...
    {144} 70f6a2 02 005408 015406  0a  00...
    {141} 70f6a2 04 01540e 01540b  90  00...
    {142} 70f6a2 0a 00aa04 015410  48  00...
    {143} 70f6a2 04 00aa0a 01541b  12  00...
    {142} 70f6a2 0c 00aa0a 01541a  ac  00...
    {144} 70f6a2 04 00aa0d 00aa0d  89  00...
    {143} 70f6a2 0c 00aa0d 00aa0d  56  00...

*/
#include "decoder.h"

static int lacrosse_r1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = { 0xd2, 0xaa, 0x2d, 0xd4 };

    uint8_t b[20];

    if (bitbuffer->num_rows > 1) {
        fprintf(stderr, "%s: Too many rows: %d\n", __func__, bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    int msg_len = bitbuffer->bits_per_row[0];
    if (msg_len < 200) { // allows shorter preamble for LTV-R3
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

    int offset = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= msg_len) {
        if (decoder->verbose) {
            fprintf(stderr, "%s: Sync word not found\n", __func__);
        }
        return DECODE_ABORT_EARLY;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 20 * 8);

    int rev = 1;
    int chk = crc8(b, 11, 0x31, 0x00);
    if (chk == 0) {
        rev = 3; // LTV-R3
    }
    else {
        chk = crc8(b, 8, 0x31, 0x00);
        if (b[10] != 0 || chk != 0) { // make sure this really is a LTV-R1 and not just a CRC collision
            if (decoder->verbose) {
                fprintf(stderr, "%s: CRC failed!\n", __func__);
            }
            return DECODE_FAIL_MIC;
        }
    }

    if (decoder->verbose) {
        bitrow_printf(b, bitbuffer->bits_per_row[0] - offset, "%s: ", __func__);
    }

    int id        = (b[0] << 16) | (b[1] << 8) | b[2];
    int flags     = (b[3] & 0xf1); // masks off seq bits
    int seq       = (b[3] & 0x0e) >> 1;
    int raw_rain1 = (b[4] << 16) | (b[5] << 8) | (b[6]);
    int raw_rain2 = (b[7] << 16) | (b[8] << 8) | (b[9]); // only LTV-R3

    // Seems rain is 0.25mm per tip, not sure what rain2 is
    float rain_mm = raw_rain1 * 0.25;
    float rain2_mm = raw_rain2 * 0.25;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_COND,   rev == 1,  DATA_STRING, "LaCrosse-R1",
            "model",            "",                 DATA_COND,   rev == 3,  DATA_STRING, "LaCrosse-R3",
            "id",               "Sensor ID",        DATA_FORMAT, "%06x",    DATA_INT, id,
            "seq",              "Sequence",         DATA_INT,    seq,
            "flags",            "unknown",          DATA_INT,    flags,
            "rain_mm",          "Total Rain",       DATA_FORMAT, "%.2f mm", DATA_DOUBLE, rain_mm,
            "rain2_mm",         "Total Rain2",      DATA_FORMAT, "%.2f mm", DATA_DOUBLE, rain2_mm,
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
        "rain_mm",
        "rain2_mm",
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
