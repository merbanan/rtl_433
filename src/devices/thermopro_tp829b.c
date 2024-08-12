/** @file
    ThermoPro TP829b Meat Thermometer four probes.

    Copyright (C) 2024 Bruno OCTAU (\@ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ThermoPro TP829b Meat Thermometer four probes.

Issue #2961 open by \@AryehGielchinsky
Product web page : https://buythermopro.com/product/tp829/

Flex decoder:

    rtl_433 -X "n=tp829b,m=FSK_PCM,s=102,l=102,r=5500,preamble=552dd4" *.cu8 2>&1 | grep codes

    codes     : {164}082f2efeddeddedde8d2d2d2d2d20000000000000

Data layout:

    Byte Position              0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20
                              II II 11 12 22 33 34 44 CC TT TT TT TT TT TT TT TT TT TT TT T
    Sample        d2 55 2d d4 08 2f 2e fe dd ed de dd e8 d2 d2 d2 d2 d2 00 00 00 00 00 00 0

- II: {16} Sensor ID, to be confirmed as the battery low not identified,
- 111:{12} Temp probe 1, °C, offset 500, scale 10,
- 222:{12} Temp probe 2, °C, offset 500, scale 10,
- 333:{12} Temp probe 3, °C, offset 500, scale 10,
- 444:{12} Temp probe 4, °C, offset 500, scale 10,
- CC: {8}  Checksum, Galois Bit Reflect Byte Reflect, gen 0x98, key 0x55, final XOR 00
- TT: Trailed bytes, not used (always d2 d2 ...... 00 00 ).

*/
static int thermopro_tp829b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = { // 0xd2, removed to increase success
                                        0x55, 0x2d, 0xd4};

    uint8_t b[9];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    int msg_len = bitbuffer->bits_per_row[0];

    if (msg_len > 260) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    int offset = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= msg_len) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    if ((msg_len - offset ) < 96 ) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 9 * 8);

    // checksum is a Galois bit reflect and byte reflect, gen 0x98, key 0x55, final XOR 00
    uint8_t b_reflect[8];
    for (int p = 7; p != -1; p += -1)
        b_reflect[7 - p] = b[p];
    int checksum = lfsr_digest8(b_reflect, 8, 0x98, 0x55);
    if (checksum != b[8]) {
        decoder_logf(decoder, 1, __func__, "Checksum error, calculated %x, expected %x", checksum, b[8]);
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 2, __func__, b, 72, "MSG");

    int id        = b[0];
    int display_u = (b[1] & 0xF0) >> 4;
    int flags     = b[1] & 0xF;
    int p1_raw    = b[2] << 4 | (b[3] & 0xF0) >> 4;
    int p2_raw    = (b[3] & 0x0F) << 8 | b[4];
    int p3_raw    = b[5] << 4 | (b[6] & 0xF0) >> 4;
    int p4_raw    = (b[6] & 0x0F) << 8 | b[7];
    float p1_temp = (p1_raw - 500) * 0.1f;
    float p2_temp = (p2_raw - 500) * 0.1f;
    float p3_temp = (p3_raw - 500) * 0.1f;
    float p4_temp = (p4_raw - 500) * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",                "",                             DATA_STRING,    "ThermoPro-TP829b",
            "id",                   "",                             DATA_FORMAT,    "%02x",      DATA_INT,    id,
            "display_u",            "Display Unit",                 DATA_COND, display_u == 0x2, DATA_STRING, "Fahrenheit",
            "display_u",            "Display Unit",                 DATA_COND, display_u == 0x0, DATA_STRING, "Celcius",
            "temperature_1_C",      "Temperature 1",                DATA_COND, p1_raw != 0xedd , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p1_temp, // if 0xedd then no probe
            "temperature_2_C",      "Temperature 2",                DATA_COND, p2_raw != 0xedd , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p2_temp, // if 0xedd then no probe
            "temperature_3_C",      "Temperature 3",                DATA_COND, p3_raw != 0xedd , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p3_temp, // if 0xedd then no probe
            "temperature_4_C",      "Temperature 4",                DATA_COND, p4_raw != 0xedd , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p4_temp, // if 0xedd then no probe
            "flags",                "Flags",                        DATA_FORMAT,    "%01x",      DATA_INT,    flags,
            "mic",                  "Integrity",                    DATA_STRING,    "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "display_u",
        "temperature_1_C",
        "temperature_2_C",
        "temperature_3_C",
        "temperature_4_C",
        "flags",
        "mic",
        NULL,
};

r_device const thermopro_tp829b = {
        .name        = "ThermoPro TP829b Meat Thermometer 4 coated probes",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 102,
        .long_width  = 102,
        .reset_limit = 5500,
        .decode_fn   = &thermopro_tp829b_decode,
        .fields      = output_fields,
};
