/** @file
    TFA 30.3307.02 wind sensor.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn static int tfa_30_3307_decode(r_device *decoder, bitbuffer_t *bitbuffer)
TFA 30.3307.02 wind sensor, part of TFA's "WeatherHub" product line.

https://github.com/merbanan/rtl_433/issues/2453

An OOK Return-to-Zero inverted (RZI) coding (see pulse_slicer_rzi()): the
signal is high almost all the time, with brief ~30 us low dips. Each dip
marks a 0-bit; the high duration between two dips (or from the start of
the message to the first dip) is a whole multiple of ~167 us and counts
that many consecutive 1-bits. Contrary to this project's usual OOK
convention (low energy/long = 0, high energy/short = 1), here the
*short* interval (the dip) is the 0-bit and *long* (a whole high run) is
one or more 1-bits.

This turns out to be the exact same raw bit-recovery approach (fill in N
"1" bits since the last detected low) used by the independent
"tfrec" project (https://github.com/baycom/tfrec, in whb.cpp) for TFA
WeatherHub sensors -- that project targets their FSK/PSK receiver chip,
but its bit-recovery algorithm is receiver-agnostic and produces the same
raw bitstream as this OOK decode. Its documented WeatherHub protocol
(also confirmed here, independently, against 3 real captures) is layered
on top of that raw bitstream:

- differential PSK decode: a new "psk" bit toggles whenever two
  consecutive raw bits are equal.
- differential NRZS decode: a new "nrzs" bit toggles whenever two
  consecutive psk bits are equal.
- G3RUH self-synchronizing descrambling: descrambled = nrzs XOR bit-16
  XOR bit-11 of a 32 bit shift register fed with nrzs bits.
- Frame sync word 0x4b2dd42b (32 bits) in the descrambled stream.

Frame, all following the sync word:

    LL II II II II II II <payload> CC CC CC CC

- LL: 1 byte, offset (from LL) of the CRC that follows the payload.
- II: 6 byte device ID; the first byte doubles as a sensor type (0x0b
  for this wind sensor; tfrec's whb.cpp documents a dozen other TFA
  WeatherHub sensor types -- temperature/humidity, rain, door/water,
  humidity guard -- not decoded here).
- CC: CRC-32 (poly 0x04c11db7) over LL and the bytes up to it; the type
  determines the CRC's init value (0xe7720ae4 for this wind sensor, from
  tfrec's crc_initvals table, confirmed against 3 real payloads here).

Wind sensor payload (27 bytes): a 3 byte sequence number, then 6 x 4 byte
historical readings (most recent first, one per ~2 minutes based on the
time field below -- only the most recent is reported here):

    IIII IIII SSSS SSSG GGGG GGGT TTTT TTT  (4 bytes, MSB first)

- I: 4 bit wind direction, x 22.5 degrees (0 = N, 90 = E, 180 = S).
- S: 9 bit wind speed (bit 8 in a separate, non-adjacent position; see
  the code), in 0.1 m/s steps.
- G: 9 bit wind gust (ditto), in 0.1 m/s steps.
- T: 8 bit age of this reading, in 2 second steps.

Verified against 3 independent real captures (2 repeats each, byte for
byte identical within each repeat pair): CRC-32 validates on all 3, all
report the same device ID (so likely the same physical sensor across
test sessions), and reported wind speed/gust are consistently 0.0 m/s
with a static direction -- physically plausible for a stationary/manual
test transmission (matching the reporter's own description of pressing
the sensor's button indoors, wind vane not exposed to real wind).
*/

static int tfa_30_3307_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = 0; // we expect a single row only
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    unsigned len     = bitbuffer->bits_per_row[row];
    uint8_t const *b = bitbuffer->bb[row];

    // De-PSK, de-NRZS, then G3RUH descramble the raw run-length-decoded
    // bits (see file header), looking for the frame sync word.
    int last_bit = 0, psk = 0, last_psk = 0, nrzs = 0;
    uint32_t lfsr = 0;
    uint32_t sr   = 0;
    int sr_cnt    = -1;

    uint8_t rdata[48];
    int byte_cnt = 0;

    for (unsigned i = 0; i < len && byte_cnt < (int)sizeof(rdata); i++) {
        int bit = bitrow_get_bit(b, i);
        if (bit == last_bit) {
            psk = 1 - psk;
        }
        if (psk == last_psk) {
            nrzs = 1 - nrzs;
        }
        last_bit = bit;
        last_psk = psk;

        int descrambled = nrzs ^ ((lfsr >> 16) & 1) ^ ((lfsr >> 11) & 1);
        lfsr             = (lfsr << 1) | (uint32_t)nrzs;

        sr = (sr >> 1) | ((uint32_t)descrambled << 31);

        if (sr == 0x2bd42d4b) {
            sr_cnt    = 0;
            rdata[0]  = sr & 0xff;
            rdata[1]  = (sr >> 8) & 0xff;
            rdata[2]  = (sr >> 16) & 0xff;
            byte_cnt  = 3;
        }

        if (sr_cnt == 0) {
            rdata[byte_cnt] = (sr >> 24) & 0xff;
            byte_cnt++;
        }
        if (sr_cnt >= 0) {
            sr_cnt = (sr_cnt + 1) & 7;
        }
    }

    if (byte_cnt < 12) {
        return DECODE_ABORT_LENGTH;
    }

    int plen = rdata[4];
    if (plen < 11 || plen + 4 > byte_cnt) {
        return DECODE_ABORT_LENGTH;
    }

    int type = rdata[5];
    uint32_t crc_init;
    if (type == 0x0b) {
        crc_init = 0xe7720ae4; // wind sensor
    }
    else {
        // Other TFA WeatherHub sensor types exist (see file header) but
        // are not decoded here.
        return DECODE_ABORT_EARLY;
    }

    // CRC-32, poly 0x04c11db7, bit by bit (no table).
    uint32_t crc_calc = crc_init;
    for (int i = 4; i < plen; i++) {
        crc_calc ^= (uint32_t)rdata[i] << 24;
        for (int k = 0; k < 8; k++) {
            crc_calc = (crc_calc & 0x80000000) ? (crc_calc << 1) ^ 0x04c11db7 : crc_calc << 1;
        }
    }
    uint32_t crc_msg = ((uint32_t)rdata[plen] << 24) | ((uint32_t)rdata[plen + 1] << 16)
            | ((uint32_t)rdata[plen + 2] << 8) | rdata[plen + 3];
    if (crc_calc != crc_msg) {
        return DECODE_FAIL_MIC;
    }

    uint64_t id = 0;
    for (int i = 0; i < 6; i++) {
        id = (id << 8) | rdata[5 + i];
    }

    uint8_t const *msg = &rdata[11];
    int msg_len         = plen - 11;
    if (msg_len < 7) {
        return DECODE_FAIL_SANITY;
    }

    uint32_t v = ((uint32_t)msg[3] << 24) | ((uint32_t)msg[4] << 16) | ((uint32_t)msg[5] << 8) | msg[6];
    float direction  = 22.5f * (v >> 28);
    float speed      = (((v >> 16) & 0xff) + 256 * ((v >> 25) & 1)) / 10.0f;
    float gust       = (((v >> 8) & 0xff) + 256 * ((v >> 24) & 1)) / 10.0f;

    char id_str[13];
    snprintf(id_str, sizeof(id_str), "%06x%06x", (unsigned)(id >> 24), (unsigned)(id & 0xffffff));

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "TFA-303307",
            "id",           "",             DATA_STRING, id_str,
            "wind_dir_deg", "Wind Direction", DATA_FORMAT, "%.1f", DATA_DOUBLE, (double)direction,
            "wind_avg_m_s", "Wind Speed",   DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, (double)speed,
            "wind_max_m_s", "Wind Gust",    DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, (double)gust,
            "mic",          "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "wind_dir_deg",
        "wind_avg_m_s",
        "wind_max_m_s",
        "mic",
        NULL,
};

r_device const tfa_30_3307 = {
        .name        = "TFA 30.3307.02 Wind sensor",
        .modulation  = OOK_PULSE_RZI,
        .short_width = 30,  // dip marking a 0-bit
        .long_width  = 167, // one 1-bit period
        .reset_limit = 500,
        .decode_fn   = &tfa_30_3307_decode,
        .fields      = output_fields,
};
