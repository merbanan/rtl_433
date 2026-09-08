/** @file
    La Crosse TX63U-IT solar wind sensor.

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
La Crosse TX63U-IT solar wind sensor.

FCC ID: OMO-M-12
Frequency: approximately 924 MHz.

The RF modulation is differential phase encoded and is recovered by the
rtl_433 PSK demodulator before this decoder is called.

Recovered bit stream:

- HDLC-style 0x7e opening and closing flags
- zero-bit stuffing after five consecutive one bits
- payload bytes transmitted least-significant bit first
- 9-byte de-stuffed frame

Known frame layout:

    3C C0 D0 D1 D2 D3 38 CRClo CRChi

The meanings of bytes 0x3c, 0xc0, and 0x38 are not yet known. They have
been constant in all verified captures and are therefore currently used
as protocol-family validation bytes.

Fields:

    D0 high nibble: average wind direction, 0..15
    D0 low nibble + D1: average wind speed, 0.1 m/s units
    D2 high nibble: gust direction, 0..15
    D2 low nibble + D3: gust speed, 0.1 m/s units

Direction encoding:

     0 N       1 NNE     2 NE      3 ENE
     4 E       5 ESE     6 SE      7 SSE
     8 S       9 SSW    10 SW     11 WSW
    12 W      13 WNW    14 NW     15 NNW

CRC:

    CRC-16/X-25 / CRC-16/IBM-SDLC
    polynomial 0x1021, reflected polynomial 0x8408
    initial value 0xffff
    reflected input/output
    xorout 0xffff

No unique sensor ID has been identified.
*/

#include "decoder.h"

#include <stdint.h>
#include <string.h>

#define TX63_FRAME_BYTES 9
#define TX63_FRAME_BITS 72

static int tx63_flag_at(
        uint8_t const *row,
        unsigned bit_count,
        unsigned pos)
{
    static uint8_t const flag[8] = {
            0, 1, 1, 1, 1, 1, 1, 0};
    unsigned i;

    if (pos + 8 > bit_count) {
        return 0;
    }

    for (i = 0; i < 8; ++i) {
        if (bitrow_get_bit(row, pos + i) != flag[i]) {
            return 0;
        }
    }

    return 1;
}

static uint16_t tx63_crc16_x25(
        uint8_t const *data,
        unsigned len)
{
    uint16_t crc = 0xffff;
    unsigned i;

    for (i = 0; i < len; ++i) {
        unsigned bit;

        crc ^= data[i];

        for (bit = 0; bit < 8; ++bit) {
            if (crc & 1) {
                crc = (uint16_t)((crc >> 1) ^ 0x8408);
            }
            else {
                crc >>= 1;
            }
        }
    }

    return (uint16_t)(crc ^ 0xffff);
}

static int tx63_destuff_frame(
        uint8_t const *row,
        unsigned start,
        unsigned end,
        uint8_t frame[TX63_FRAME_BYTES])
{
    uint8_t bits[TX63_FRAME_BITS];
    unsigned bit_count = 0;
    unsigned ones = 0;
    unsigned i;

    memset(frame, 0, TX63_FRAME_BYTES);

    for (i = start; i < end; ++i) {
        uint8_t bit = bitrow_get_bit(row, i);

        /*
         * HDLC inserts a zero following five consecutive one bits.
         * Remove that stuffed zero before reconstructing the payload.
         */
        if (ones == 5) {
            if (bit != 0) {
                return 0;
            }

            ones = 0;
            continue;
        }

        if (bit_count >= TX63_FRAME_BITS) {
            return 0;
        }

        bits[bit_count++] = bit;

        if (bit) {
            ++ones;
        }
        else {
            ones = 0;
        }
    }

    if (bit_count != TX63_FRAME_BITS) {
        return 0;
    }

    /*
     * Payload bytes are transmitted LSB first.
     */
    for (i = 0; i < TX63_FRAME_BITS; ++i) {
        if (bits[i]) {
            frame[i / 8] |= (uint8_t)(1U << (i % 8));
        }
    }

    return 1;
}

static int tx63_find_frame(
        bitbuffer_t const *bitbuffer,
        uint8_t frame[TX63_FRAME_BYTES])
{
    uint8_t const *row;
    unsigned bit_count;
    unsigned start_flag;

    if (!bitbuffer || bitbuffer->num_rows != 1) {
        return 0;
    }

    row = bitbuffer->bb[0];
    bit_count = bitbuffer->bits_per_row[0];

    for (start_flag = 0;
            start_flag + 8 <= bit_count;
            ++start_flag) {
        unsigned end_flag;

        if (!tx63_flag_at(row, bit_count, start_flag)) {
            continue;
        }

        /*
         * Verified captures place the closing flag 75 to 110
         * recovered bits after the opening flag.
         */
        for (end_flag = start_flag + 75;
                end_flag <= start_flag + 110 &&
                end_flag + 8 <= bit_count;
                ++end_flag) {
            uint16_t received_crc;
            uint16_t calculated_crc;

            if (!tx63_flag_at(row, bit_count, end_flag)) {
                continue;
            }

            if (!tx63_destuff_frame(
                        row,
                        start_flag + 8,
                        end_flag,
                        frame)) {
                continue;
            }

            /*
             * Fixed values observed in every verified TX63 capture.
             * Their precise semantic meaning is not yet known.
             */
            if (frame[0] != 0x3c ||
                    frame[1] != 0xc0 ||
                    frame[6] != 0x38) {
                continue;
            }

            received_crc =
                    (uint16_t)frame[7] |
                    ((uint16_t)frame[8] << 8);

            calculated_crc =
                    tx63_crc16_x25(frame, 7);

            if (received_crc != calculated_crc) {
                continue;
            }

            return 1;
        }
    }

    return 0;
}

/*
 * Pure protocol validator for PSK rate/phase/polarity hypothesis testing.
 *
 * This routine deliberately produces no rtl_433 output and does not modify
 * decoder statistics. The normal decode function is called only after the
 * PSK layer has selected a valid bitbuffer.
 */
int lacrosse_tx63_validate(
        bitbuffer_t *bitbuffer,
        void *context)
{
    uint8_t frame[TX63_FRAME_BYTES];

    (void)context;

    return tx63_find_frame(bitbuffer, frame);
}

static int lacrosse_tx63_decode(
        r_device *decoder,
        bitbuffer_t *bitbuffer)
{
    uint8_t frame[TX63_FRAME_BYTES];
    unsigned direction_code;
    unsigned wind_raw;
    unsigned gust_direction_code;
    unsigned gust_raw;
    double wind_direction_deg;
    double gust_direction_deg;
    double wind_speed_m_s;
    double gust_speed_m_s;
    data_t *data;

    if (!bitbuffer || bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[0] < 90) {
        return DECODE_ABORT_LENGTH;
    }

    if (!tx63_find_frame(bitbuffer, frame)) {
        return DECODE_FAIL_MIC;
    }

    direction_code =
            (frame[2] >> 4) & 0x0f;

    wind_raw =
            ((unsigned)(frame[2] & 0x0f) << 8) |
            frame[3];

    gust_direction_code =
            (frame[4] >> 4) & 0x0f;

    gust_raw =
            ((unsigned)(frame[4] & 0x0f) << 8) |
            frame[5];

    wind_direction_deg =
            direction_code * 22.5;

    gust_direction_deg =
            gust_direction_code * 22.5;

    wind_speed_m_s =
            wind_raw * 0.1;

    gust_speed_m_s =
            gust_raw * 0.1;

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "LaCrosse-TX63U-IT",
            "wind_avg_m_s",     "Wind speed",       DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind_speed_m_s,
            "wind_max_m_s",     "Wind gust",        DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, gust_speed_m_s,
            "wind_dir_deg",     "Wind direction",   DATA_FORMAT, "%.1f", DATA_DOUBLE, wind_direction_deg,
            "wind_max_dir_deg", "Gust direction",   DATA_FORMAT, "%.1f", DATA_DOUBLE, gust_direction_deg,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "wind_avg_m_s",
        "wind_max_m_s",
        "wind_dir_deg",
        "wind_max_dir_deg",
        "mic",
        NULL,
};

r_device const lacrosse_tx63 = {
        .name        = "La Crosse TX63U-IT solar wind sensor",
        .modulation  = PSK_PULSE_DBPSK,
        .decode_fn   = &lacrosse_tx63_decode,
        .validate_fn = &lacrosse_tx63_validate,
        .fields      = output_fields,
};