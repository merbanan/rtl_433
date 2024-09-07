/** @file
    ELV WS 2000.

    KS200/KS300 addition Copyright (C) 2022 Jan Schmidt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

static uint16_t AD_POP(uint8_t const *bb, uint8_t bits, uint8_t bit)
{
    uint16_t val = 0;
    for (uint8_t i = 0; i < bits; i++) {
        uint8_t byte_no = (bit + i) / 8;
        uint8_t bit_no  = 7 - ((bit + i) % 8);
        if (bb[byte_no] & (1 << bit_no)) {
            val = val | (1 << i);
        }
    }
    return val;
}

/**
ELV EM 1000 decoder.

based on fs20.c
*/
static int em1000_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitrow_t *bb = bitbuffer->bb;
    uint8_t dec[10];
    uint8_t bit = 18; // preamble
    uint8_t bb_p[14];
    //char* types[] = {"EM 1000-S", "EM 1000-?", "EM 1000-GZ"};
    uint8_t checksum_calculated = 0;
    uint8_t i;
    uint8_t checksum_received;

    // check and combine the 3 repetitions
    for (i = 0; i < 14; i++) {
        if (bb[0][i] == bb[1][i] || bb[0][i] == bb[2][i])
            bb_p[i] = bb[0][i];
        else if (bb[1][i] == bb[2][i])
            bb_p[i] = bb[1][i];
        else
            return DECODE_ABORT_EARLY;
    }

    // read 9 bytes with stopbit ...
    for (i = 0; i < 9; i++) {
        dec[i] = AD_POP(bb_p, 8, bit);
        bit += 8;
        uint8_t stopbit = AD_POP(bb_p, 1, bit);
        bit += 1;
        if (!stopbit) {
//            decoder_logf(decoder, 0, __func__, "!stopbit: %d", i);
            return DECODE_ABORT_EARLY;
        }
        checksum_calculated ^= dec[i];
    }

    // Read checksum
    checksum_received = AD_POP(bb_p, 8, bit); // bit+=8;
    if (checksum_received != checksum_calculated) {
//        decoder_logf(decoder, 0, __func__, "checksum_received != checksum_calculated: %d %d", checksum_received, checksum_calculated);
        return DECODE_FAIL_MIC;
    }

//    decoder_log_bitrow(decoder, 0, __func__, dec, 9 * 8, "");

    // based on 15_CUL_EM.pm
    //char *subtype = dec[0] >= 1 && dec[0] <= 3 ? types[dec[0] - 1] : "?";
    int code      = dec[1];
    int seqno     = dec[2];
    int total     = dec[3] | dec[4] << 8;
    int current   = dec[5] | dec[6] << 8;
    int peak      = dec[7] | dec[8] << 8;

    /* clang-format off */
    data_t *data = data_make(
            "model",    "", DATA_STRING, "ELV-EM1000",
            "id",       "", DATA_INT, code,
            "seq",      "", DATA_INT, seqno,
            "total",    "", DATA_INT, total,
            "current",  "", DATA_INT, current,
            "peak",     "", DATA_INT, peak,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const elv_em1000_output_fields[] = {
        "model",
        "id",
        "seq",
        "total",
        "current",
        "peak",
        NULL,
};

r_device const elv_em1000 = {
        .name        = "ELV EM 1000",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 500,  // guessed, no samples available
        .long_width  = 1000, // guessed, no samples available
        .gap_limit   = 7250,
        .reset_limit = 30000,
        .decode_fn   = &em1000_callback,
        .disabled    = 1,
        .fields      = elv_em1000_output_fields,
};

/**
ELV WS 2000.

based on http://www.dc3yc.privat.t-online.de/protocol.htm

- added support for comob sensor (subtyp 7)
- sensor 1 (Thermo/Hygro) and 4 (Thermo/Hygro/Baro) supported as well
- other sensors could be detected, if the length is defined correct
  but will not receive correct values

- rain_count counts the ticks of a seesaw, the amount of water per
  tick has to be calibrated. As shown in user manual the default is
  295ml / m²

Protocol version V1.2

Coding of a bit:
- the length of a bit is 1220.7s, corresponding to 819.2 Hz
- it is derived from 32768 Hz : 40
- the pulse:gap ratio is 7:3 (for logical 0) or 3:7 (for logical 1)
- a logical 0 is represented by an HF carrier of 854.5s and 366.2s gap
- a logical 1 is represented by a HF carrier of 366.2s and 854.5s gap
- The preamble consists of 7 to 10 * 0 and 1 * 1.
- The data is always transmitted as a 4-bit nibble. This is followed by a 1 bit.
- The LSBit is transmitted first.

The checksums at the end are calculated as follows:
- Check: all nibbles starting with the type up to Check are XORed, result is 0
- Sum: all nibbles beginning with the type up to Check are summed up,
  5 is added and the upper 4 bits are discarded

The type consists of 3 bits encoded as follows.
- 0 Thermal (AS3)
- 1 Thermo/Hygro (AS2000, ASH2000, S2000, S2001A, S2001IA, ASH2200, S300IA)
- 2 Rain (S2000R)
- 3 Wind (S2000W)
- 4 Thermo/Hygro/Baro (S2001I, S2001ID)
- 5 Brightness (S2500H)
- 6 Pyrano (radiant power)
- 7 Combo Sensor (KS200, KS300)

    00000001  T1T2T3T41  A1A2A3A41  T11T12T13T141  T21T22T23T241  T31T32T33T341  F11F12F13F141  F21F22F23F241  W11W12W13W141  W21W22W23W241  W31W32W33W341  C11C12C13C141  C21C22C23C241  C31C32C33C341  B1B2B3B41  Q1Q2Q3Q41  S1S2S3S41
    Preamble  ____7___1  1_R_0_V_1  ____0.1C____1  ____1C______1  _____10C____1  _____1%_____1  ____10%_____1  __0.1 km/h__1  ___1 km/h___1  ___10 km/h__1  ___R_LSN____1  ___R_MID____1  ____R_MSN___1  ___???__1  __Check_1  __Sum___1

- R: Currently Raining (1 = rain)
- V: Temperature sign (1 = negative)
- W1x .. W3x : 3 * 4-bit wind speed km/h (BCD)
- C1x .. C3x :    12-bit rain counter
- T1x .. T3x : 3 * 4-bit temperature C (BCD)
- F1x .. F2x : 2 * 4-bit humidity % (BCD)
 */
static int ws2000_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitrow_t *bb = bitbuffer->bb;
    uint8_t dec[16]= {0};
    uint8_t nibbles=0;
    uint8_t bit=11; // preamble
    char const *const types[]={"!AS3", "AS2000/ASH2000/S2000/S2001A/S2001IA/ASH2200/S300IA", "!S2000R", "!S2000W", "S2001I/S2001ID", "!S2500H", "!Pyrano", "KS200/KS300"};
    uint8_t const length[16] = {5, 8, 5, 8, 12, 9, 8, 14, 8};
    uint8_t check_calculated=0, sum_calculated=0;
    uint8_t i;
    uint8_t stopbit;
    uint8_t sum_received;

    dec[0] = AD_POP(bb[0], 4, bit); bit+=4;
    stopbit= AD_POP(bb[0], 1, bit); bit+=1;
    if (!stopbit) {
        decoder_log(decoder, 1, __func__, "!stopbit");
        return DECODE_ABORT_EARLY;
    }
    check_calculated ^= dec[0];
    sum_calculated   += dec[0];

    // read nibbles with stopbit ...
    for (i = 1; i <= length[dec[0]]; i++) {
        dec[i] = AD_POP(bb[0], 4, bit); bit+=4;
        stopbit= AD_POP(bb[0], 1, bit); bit+=1;
        if (!stopbit) {
            decoder_logf(decoder, 1, __func__, "!stopbit %d", bit);
            return DECODE_ABORT_EARLY;
        }
        check_calculated ^= dec[i];
        sum_calculated   += dec[i];
        nibbles++;
    }
    decoder_log_bitrow(decoder, 1, __func__, dec, nibbles * 8, "");

    if (check_calculated) {
        decoder_logf(decoder, 1, __func__, "check_calculated (%d) != 0", check_calculated);
        return DECODE_FAIL_MIC;
    }

    // Read sum
    sum_received = AD_POP(bb[0], 4, bit); // bit+=4;
    sum_calculated+=5;
    sum_calculated&=0xF;
    if (sum_received != sum_calculated) {
        decoder_logf(decoder, 1, __func__, "sum_received (%d) != sum_calculated (%d)", sum_received, sum_calculated);
        return DECODE_FAIL_MIC;
    }

    char const *subtype  = (dec[0] <= 7) ? types[dec[0]] : "?";
    int code       = dec[1] & 7;
    float temp     = ((dec[1] & 8) ? -1.0f : 1.0f) * (dec[4] * 10 + dec[3] + dec[2] * 0.1f);
    float humidity = dec[7] * 10 + dec[6] + dec[5] * 0.1f;
    int pressure   = 0;

    int is_ksx00 = 0;
    int it_rains = 0;
    float wind   = 0;
    int rainsum  = 0;
    int unknown  = 0;
    if (dec[0] == 4) {
        pressure = 200 + dec[10] * 100 + dec[9] * 10 + dec[8];
    }
    if (dec[0] == 7) {
        is_ksx00 = 1;
        it_rains = (dec[1] & 2) ? 1 : 0;
        humidity = dec[6] * 10 + dec[5];
        wind     = dec[9] * 10 + dec[8] + dec[7] * 0.1f;
        rainsum  = (dec[12] << 8) + (dec[11] << 4) + dec[10];
        unknown  = dec[13];
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",            "", DATA_STRING, "ELV-WS2000",
            "subtype",          "", DATA_STRING, subtype,
            "id",               "", DATA_INT,    code,
            "temperature_C",    "", DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)temp,
            "humidity",         "", DATA_FORMAT, "%.1f %%", DATA_DOUBLE, (double)humidity,
            "pressure_hPa",     "", DATA_COND, pressure, DATA_FORMAT, "%d hPa", DATA_INT, pressure,
            //KS200 / KS300
            "wind_avg_km_h",    "", DATA_COND, is_ksx00, DATA_FORMAT, "%.1f kmh", DATA_DOUBLE, (double)wind,
            "rain_count",       "", DATA_COND, is_ksx00, DATA_FORMAT, "%d", DATA_INT, rainsum,
            "rain_mm",          "", DATA_COND, is_ksx00, DATA_FORMAT, "%.1f", DATA_DOUBLE, (double)rainsum * 0.295,
            "is_raining",       "", DATA_COND, is_ksx00, DATA_FORMAT, "%d", DATA_INT, it_rains,
            "unknown",          "", DATA_COND, is_ksx00, DATA_FORMAT, "%d", DATA_INT, unknown,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const elv_ws2000_output_fields[] = {
        "model",
        "id",
        "subtype",
        "temperature_C",
        "humidity",
        "pressure_hPa",
        // KS200 / KS300
        "wind_avg_km_h",
        "rain_count",
        "rain_mm",
        "is_raining",
        "unknown",
        NULL,
};

r_device const elv_ws2000 = {
        .name        = "ELV WS 2000",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 366,  // 0 => 854us, 1 => 366us according to link in top
        .long_width  = 854,  // no repetitions
        .reset_limit = 1000, // Longest pause is 854us according to link
        .decode_fn   = &ws2000_callback,
        .disabled    = 1,
        .fields      = elv_ws2000_output_fields,
};
