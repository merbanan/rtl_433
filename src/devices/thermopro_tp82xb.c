/** @file
    ThermoPro TP82xB Meat Thermometer probes.

    Copyright (C) 2024 Bruno OCTAU (\@ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ThermoPro TP82xB Meat Thermometer probes.

TP829B, 4 Probes, simple Temperature:
- Issue #2961 open by \@AryehGielchinsky
- Product web page : https://buythermopro.com/product/tp829/

TP828P, 2 Probes, current Temperature, BBQ LO and HI temperatures:
- Issue #3082 open by Ryan Bray (\@rbray89)
- Product archive web page: http://web.archive.org/web/20240717222907/https://buythermopro.com/product/tp828w/
- FCCID : https://fccid.io/2AATP-TP828B

Flex decoder:

    rtl_433 -X "n=tp829b,m=FSK_PCM,s=102,l=102,r=5500,preamble=552dd4" *.cu8 2>&1 | grep codes

    TP829B codes: {164}082f2efeddeddedde8d2d2d2d2d20000000000000
    TP828B codes: {164}772c2eceaa4f3eddeaa4d7b2d2d2d2d2d20000000

Data layout TP829B:

    Byte Position              0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20
                              II UF 11 12 22 33 34 44 CC TT TT TT TT TT TT TT TT TT TT TT T
    Sample        d2 55 2d d4 08 2f 2e fe dd ed de dd e8 d2 d2 d2 d2 d2 00 00 00 00 00 00 0

- II:  {8} Sensor ID,
- U:   {4} Temp Unit Display, 0x0 for Celsius, 0x2 for Fahrenheit,
- F:   {4} Unknown flags, always 0xF, to be confirmed as the battery low not identified,
- 111:{12} Temp probe 1, °C, offset 500, scale 10,
- 222:{12} Temp probe 2, °C, offset 500, scale 10,
- 333:{12} Temp probe 3, °C, offset 500, scale 10,
- 444:{12} Temp probe 4, °C, offset 500, scale 10,
- CC:  {8} Checksum, Galois Bit Reflect Byte Reflect, gen 0x98, key 0x55, final XOR 0x00,
- TT:      Trailed bytes, not used (always d2 d2 ...... 00 00 ).

Data layout TP828B:

    Byte Position              0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20
                              II UF 11 11 11 11 12 22 22 22 22 CC TT TT TT TT TT TT TT TT T
                                    PP PL LL HH HP PP LL LH HH
    Sample        d2 55 2d d4 77 2c 2e ce aa 4f 3e dd ea a4 d7 b2 d2 d2 d2 d2 d2 00 00 00 0

- II:      {8} Sensor ID,
- U:       {4} Temp Unit Display, 0x0 for Celsius, 0x2 for Fahrenheit,
- F:       {4} Unknown flags, always 0xC, to be confirmed as the battery low not identified,
- 111/PPP:{12} Probe 1 Current Temp , °C, offset 500, scale 10,
- 111/LLL:{12} Probe 1 Target LO Temp, °C, offset 500, scale 10,
- 111/HHH:{12} Probe 1 Target HI Temp, °C, offset 500, scale 10,
- 222/PPP:{12} Probe 2 Current Temp, °C, offset 500, scale 10,
- 222/LLL:{12} Probe 2 Target LO Temp, °C, offset 500, scale 10,
- 222/HHH:{12} Probe 2 Target HI Temp, °C, offset 500, scale 10,
- CC: {8}  Checksum, Galois Bit Reflect Byte Reflect, gen 0x98, key 0x16, final XOR 0xac,
- TT: Trailed bytes, not used (always d2 d2 ...... 00 00 ).

*/
static int thermopro_tp82xb_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = { // 0xd2, removed to increase success
                                        0x55, 0x2d, 0xd4};
    // Message len is 9 byte for tp829b and 12 byte for tp828b
    uint8_t b[12];
    uint8_t tp829b_reflect[8];
    uint8_t tp828b_reflect[11];
    uint8_t model = 0;
    uint8_t ret   = 0;
    int  checksum = 0;
    int  p        = 0;

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
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 12 * 8);

    // Need to define the type of sensor, if b[9], b[10],b[11] = 0xd2, could be a tp829b, then need to control the checksum accordingly to confirm
    // checksum is a Galois bit reflect and byte reflect, gen 0x98, key 0x55, final XOR 0x00
    if ( b[9] == 0xd2 && b[10] == 0xd2 && b[10] == 0xd2) {
        for (p = 7; p != -1; p += -1)
            tp829b_reflect[7 - p] = b[p];
        checksum = lfsr_digest8(tp829b_reflect, 8, 0x98, 0x55);
        if (checksum != b[8]) {
            decoder_logf(decoder, 1, __func__, "Checksum error, calculated %x, expected %x, not tp829b model", checksum, b[8]);
            ret = DECODE_FAIL_MIC;
        }
        else 
            model = 9; // ie tp829b
    }

    // Now test if model tp828b
    if (model == 0) {
        for (p = 10; p != -1; p += -1)
            tp828b_reflect[10 - p] = b[p];
        checksum = lfsr_digest8(tp828b_reflect, 11, 0x98, 0x16) ^ 0xac ;
        if (checksum != b[11]) {
            decoder_logf(decoder, 1, __func__, "Checksum error, calculated %x, expected %x, not tp828b model", checksum, b[11]);
            ret = DECODE_FAIL_MIC;
        }
        else 
            model = 8; // ie tp828b
    }

    // Decode_Fail_Mic
    if (ret)
        return ret;

    // If model = tp828b
    if (model == 8) {
        decoder_log_bitrow(decoder, 2, __func__, b, 96, "MSG");

        int id        = b[0];
        int display_u = (b[1] & 0xF0) >> 4;
        int flags     = b[1] & 0xF;
        int p1_raw    = b[2] << 4 | (b[3] & 0xF0) >> 4;
        int p1_lo_raw = (b[3] & 0x0F) << 8 | b[4];
        int p1_hi_raw = b[5] << 4 | (b[6] & 0xF0) >> 4;
        int p2_raw    = (b[6] & 0x0F) << 8 | b[7];
        int p2_lo_raw = b[8] << 4 | (b[9] & 0xF0) >> 4;
        int p2_hi_raw = (b[9] & 0x0F) << 8 | b[10];

        float p1_temp = (p1_raw - 500)    * 0.1f;
        float p1_t_lo = (p1_lo_raw - 500) * 0.1f;
        float p1_t_hi = (p1_hi_raw - 500) * 0.1f;
        float p2_temp = (p2_raw - 500)    * 0.1f;
        float p2_t_lo = (p2_lo_raw - 500) * 0.1f;
        float p2_t_hi = (p2_hi_raw - 500) * 0.1f;

        /* clang-format off */
        data_t *data = data_make(
                "model",                "",                             DATA_STRING,    "ThermoPro-TP828b",
                "id",                   "",                             DATA_FORMAT,    "%02x",         DATA_INT,    id,
                "display_u",            "Display Unit",                 DATA_COND, display_u == 0x2,    DATA_STRING, "Fahrenheit",
                "display_u",            "Display Unit",                 DATA_COND, display_u == 0x0,    DATA_STRING, "Celsius",
                "temperature_1_C",      "Temperature 1",                DATA_COND, p1_raw != 0xedd ,    DATA_FORMAT, "%.1f C", DATA_DOUBLE, p1_temp, // if 0xedd then no probe
                "temperature_1_LO_C",   "Temperature 1 LO",             DATA_COND, p1_lo_raw != 0xeaa , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p1_t_lo, // if 0xeaa then no LO
                "temperature_1_HI_C",   "Temperature 1 HI",                                             DATA_FORMAT, "%.1f C", DATA_DOUBLE, p1_t_hi,
                "temperature_2_C",      "Temperature 2",                DATA_COND, p2_raw != 0xedd ,    DATA_FORMAT, "%.1f C", DATA_DOUBLE, p2_temp, // if 0xedd then no probe
                "temperature_2_LO_C",   "Temperature 2 LO",             DATA_COND, p2_lo_raw != 0xeaa , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p2_t_lo, // if 0xeaa then no LO
                "temperature_2_HI_C",   "Temperature 2 HI",                                             DATA_FORMAT, "%.1f C", DATA_DOUBLE, p2_t_hi,
                "flags",                "Flags",                        DATA_FORMAT,    "%01x",         DATA_INT,    flags,
                "mic",                  "Integrity",                    DATA_STRING,    "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    // If model = tp829b
    else if (model == 9) {
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
                "display_u",            "Display Unit",                 DATA_COND, display_u == 0x0, DATA_STRING, "Celsius",
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
    return 0;

}

static char const *const output_fields[] = {
        "model",
        "id",
        "display_u",
        "temperature_1_C",
        "temperature_2_C",
        "temperature_3_C",
        "temperature_4_C",
        "temperature_1_LO_C",
        "temperature_2_LO_C",
        "temperature_1_HI_C",
        "temperature_2_HI_C",
        "flags",
        "mic",
        NULL,
};

r_device const thermopro_tp82xb = {
        .name        = "ThermoPro Meat Thermometer ",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 102,
        .long_width  = 102,
        .reset_limit = 5500,
        .decode_fn   = &thermopro_tp82xb_decode,
        .fields      = output_fields,
};
