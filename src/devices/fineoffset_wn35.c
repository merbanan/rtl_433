/** @file
    Fine Offset Electronics WN35 Leaf Wetness sensor.

    Copyright (C) 2026 Christian W. Zuckschwerdt <zany@triq.net>
    Analyzed by Hans Niekus \@hansniekus

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Fine Offset Electronics WN35 Leaf Wetness sensor.

S.a. issue #3665

Seen at a frequency of 868.35M.

Preamble is aaa aaaa aaaa, sync word is 2dd4.

Packet layout:

     0  1  2  3  4  5  6  7  8  9 10
    YY II II II RR RR RR WW VV XX AA
    35 00 30 5E 01 06 55 07 55 70 EB

- Y:{8}  (byte 0): Sensor Type 0x35
- I:{24} (bytes 1-3): ID (24 bit)
- R:{24} (bytes 4-6): Raw A/D value (probably)
- W:{8}  (byte 7): Wetness
- V:{8}  (byte 8): Battery Level (unit of 20 mV)
- X:{8}  (byte 9): CRC
- A:{8}  (byte 10): Checksum

*/

static int fineoffset_wn35_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xaa, 0x2d, 0xd4};
    uint8_t b[11];

    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8;
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) {  // Did not find a long enough package
        decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "short package. Row length: %u. Header index: %u", bitbuffer->bits_per_row[0], bit_offset);
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    // Verify family code
    if (b[0] != 0x35) {
        decoder_logf(decoder, 2, __func__, "Msg family unknown: %02x", b[0]);
        decoder_log_bitbuffer(decoder, 2, __func__, bitbuffer, "");
        return DECODE_ABORT_EARLY;
    }

    decoder_log_bitrow(decoder, 1, __func__, b, sizeof (b) * 8, "");

    // Verify checksum, same as other FO Stations: Reverse 1Wire CRC (poly 0x131)
    uint8_t crc = crc8(b, 10, 0x31, 0x00);
    uint8_t chk = add_bytes(b, 10);

    if (crc != 0 || chk != b[10]) {
        decoder_logf(decoder, 2, __func__, "Checksum error: %02x %02x", crc, chk);
        return DECODE_FAIL_MIC;
    }

    // Decode data fields
    int id         = (b[1] << 16) | (b[2] << 8) | (b[3]);
    int raw_value  = (b[4] << 16) | (b[5] << 8) | (b[6]);
    int wetness    = b[7];
    int battery_mv = b[8] * 20;

    // voltage breakpoints copied from WN34, needs a review.
    int battery_pct;

    if (battery_mv > 1440) {
        battery_pct = 100;
    }
    else if (battery_mv > 1380) {
        battery_pct = 80;
    }
    else if (battery_mv > 1300) {
        battery_pct = 60;
    }
    else if (battery_mv > 1200) {
        battery_pct = 40;
    }
    else {
        battery_pct = 20;
    }

    int battery_ok  = battery_pct > 20;

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",                 DATA_STRING, "Fineoffset-WN35",
            "id",           "ID",               DATA_FORMAT, "%06x",     DATA_INT,   id,
            "battery_ok",   "Battery",          DATA_INT,    battery_ok,
            "battery_pct",  "Battery level",    DATA_FORMAT, "%d %%",   DATA_INT,   battery_pct,
            "battery_mV",   "Battery voltage",  DATA_FORMAT, "%d mV",   DATA_INT,   battery_mv,
            "wetness",      "Wetness",          DATA_INT,    wetness,
            "raw",          "Raw value",        DATA_INT,    raw_value,
            "mic",          "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "battery_pct",
        "battery_mV",
        "wetness",
        "raw",
        "mic",
        NULL,
};

r_device const fineoffset_wn35 = {
        .name        = "Fine Offset Electronics WN35 Leaf Wetness sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 2500,
        .decode_fn   = &fineoffset_wn35_decode,
        .fields      = output_fields,
};
