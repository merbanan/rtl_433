/** @file
    EezTire E618 TPMS and Carchet TPMS (same protocol).

    Copyright (C) 2023 Bruno OCTAU (ProfBoc75), Gliebig, and Karen Suhm

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
EezTire E618 TPMS and Carchet TPMS (same protocol).

- Eez RV supported TPMS sensor model E618 : https://eezrvproducts.com/shop/ols/products/tpms-system-e518-anti-theft-replacement-sensor-1-ea
- Carchet TPMS: http://carchet.easyofficial.com/carchet-rv-trailer-car-solar-tpms-tire-pressure-monitoring-system-6-sensor-lcd-display-p6.html
- TST (Truck Systems Technologies) 507 Series TPMS : https://github.com/cterwilliger/tst_tpms

The device uses OOK (ASK) encoding.
The device sends a transmission every 1 second when quick deflation is detected, every 13 - 23 sec when quick inflation is detected, and every 4 min 40 s under steady state pressure.
A transmission starts with a preamble of 0x0000 and the packet is sent twice.

S.a issue #2384, #2657, #2063, #2677, #2819

Data collection parameters on URH software were as follows:
    Sensor frequency: 433.92 MHz
    Sample rate: 2.0 MSps
    Bandwidth: 2.0 Hz
    Gain: 125

    Modulation is ASK (OOK). Packets in URH arrive in the following format:

    aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa [Pause: 897679 samples]
    aaaaaaaa5956a5a5a6555aaa65959999a5aaaaaa [Pause: 6030 samples]
    aaaaaaaa5956a5a5a6555aaa65959999a5aaaaaa [Pause: 11176528 samples]

    Decoding is Manchester I.  After decoding, the packets look like this:

    00000000000000000000000000000000000000
    0000de332fc0b7553000
    0000de332fc0b7553000

 Using rtl_433 software, packets were detected using the following command line entry:
 rtl_433 -X "n=Carchet,m=OOK_MC_ZEROBIT,s=50,l=50,r=1000,invert" -s 1M

 The decoder slices the half-bits as PCM with 50 us width and Manchester decodes
 the row (IEEE 802.3, high-low is a 0), which validates every symbol. This yields
 the data below directly, only the preamble reads as 0000 instead of FFFF.

 Data layout:

    PRE CC IIIIII PP TT FF FF

- PRE : FFFF
- C : 8 bit CheckSum, modulo 256 with overflow flag
- I: 24 bit little-endian ID
- P: 8 bit pressure  P * 2.5 = Pressure kPa
- T: 8 bit temperature   T - 50 = Temperature C
- F: 16 bit status flags: 0x8000 = low battery, 0x1000 = quick deflation, 0x3000 = quick inflation, 0x0000 = static/steady state

Raw Data example :

    ffff 8b 0d177e 8f 4a 10 00

Format string:

    CHECKSUM:8h ID:24h KPA:8d TEMP:8d FLAG:8b 8b

Decode example:

    CHECKSUM:8b ID:0d177e KPA:8f TEMP:4a FLAG:10 00

*/

static int tpms_eezrv_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }
    // 2 bytes preamble + 1 byte chk + 7 byte data are 80 Manchester coded bits,
    // i.e. 160 PCM half-bits plus a trailing bit
    if (bitbuffer->bits_per_row[0] < 160) {
        return DECODE_ABORT_LENGTH;
    }
    bitbuffer_t packet = {0};
    bitbuffer_manchester_decode(bitbuffer, 0, 0, &packet, 0);
    // The decode stops at the first invalid Manchester symbol, the whole packet
    // must be valid and exactly 80 bits, a longer run is e.g. the preamble burst
    if (packet.bits_per_row[0] != 10 * 8) {
        decoder_logf(decoder, 2, __func__, "Manchester length check fail (%u bits)", packet.bits_per_row[0]);
        return DECODE_ABORT_LENGTH;
    }
    uint8_t b[7]  = {0};
    uint8_t cc[1] = {0};
    bitbuffer_extract_bytes(&packet, 0, 16, cc, sizeof(cc) * 8);
    bitbuffer_extract_bytes(&packet, 0, 24, b, sizeof(b) * 8);

    // Verify checksum
    // If the checksum is greater than 0xFF then the MSB is set.
    // It occurs whether the bit is already set or not and was observed when checksum was in the 0x1FF and the 0x2FF range.
    int computed_checksum = add_bytes(b, sizeof(b));
    if (computed_checksum > 0xff) {
        computed_checksum |= 0x80;
    }

    if ((computed_checksum & 0xff) != cc[0]) {
        decoder_log(decoder, 2, __func__, "Checksum fail");
        return DECODE_FAIL_MIC;
    }

    // Reject iMars T240 frames, see tpms_imars_t240.c. That protocol shares the
    // modulation and the 80 bit length, and its frames can pass the 8 bit checksum.
    // The iMars bytes B0..B7 are the inverted check byte and data bytes, the frame
    // has B7 == B0 and matching low nibbles in B0 and B1. The per-unit checksum
    // constant is not tested, so iMars units not known to that decoder match too.
    uint8_t m[8];
    m[0] = ~cc[0];
    for (unsigned i = 0; i < sizeof(b); ++i) {
        m[i + 1] = ~b[i];
    }
    if (m[7] == m[0] && (m[0] & 0x0f) == (m[1] & 0x0f)) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY iMars T240 frame");
        return DECODE_FAIL_SANITY;
    }

    int flags1 = b[5];
    int flags2 = b[6];

    // Known flags1 bits are 0x80 low battery, 0x20 inflating, 0x10 fast leak and
    // 0x40 observed but unknown, the low nibble is always clear. Known flags2 bits
    // are 0x01 the pressure MSB and 0x20 seen during low battery testing.
    if ((flags1 & 0x0f) || (flags2 & 0xde)) {
        decoder_logf(decoder, 2, __func__, "DECODE_FAIL_SANITY unknown flags %02x%02x", flags1, flags2);
        return DECODE_FAIL_SANITY;
    }

    int temperature_C      = b[4] - 50;
    int fast_leak_detected = (flags1 & 0x10);      // fast leak - reports every second
    int infl_detected      = (flags1 & 0x20) >> 5; // inflating - reports every 15 - 20 sec

    int fast_leak      = fast_leak_detected && !infl_detected;
    float pressure_kPa = (((flags2 & 0x01) << 8) + b[3]) * 2.5f;

    // Low batt = 0x8000;
    int low_batt = flags1 >> 7; // Low batt flag is MSB (activated at V < 3.15 V)(Device fails at V < 3.10 V)
    // Mystery flag at (flags2 & 0x20) showed up during low batt testing

    char id_str[7];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x", b[0], b[1], b[2]);

    char flags_str[5];
    snprintf(flags_str, sizeof(flags_str), "%02x%02x", flags1, flags2);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "EezTire-E618",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "battery_ok",       "Battery_OK",   DATA_INT,    !low_batt,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure_kPa,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)temperature_C,
            "flags",            "Flags",        DATA_STRING, flags_str,
            "fast_leak",        "Fast Leak",    DATA_INT,    fast_leak,
            "inflate",          "Inflate",      DATA_INT,    infl_detected,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "battery_ok",
        "pressure_kPa",
        "temperature_C",
        "flags",
        "fast_leak",
        "inflate",
        "mic",
        NULL,
};

r_device const tpms_eezrv = {
        .name        = "EezTire E618, Carchet TPMS, TST-507 TPMS",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 50,
        .long_width  = 50,
        .reset_limit = 120,
        .decode_fn   = &tpms_eezrv_decode,
        .fields      = output_fields,
};
