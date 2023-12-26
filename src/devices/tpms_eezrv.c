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

Eez RV supported TPMS sensor model E618 : https://eezrvproducts.com/shop/ols/products/tpms-system-e518-anti-theft-replacement-sensor-1-ea
Carchet TPMS: http://carchet.easyofficial.com/carchet-rv-trailer-car-solar-tpms-tire-pressure-monitoring-system-6-sensor-lcd-display-p6.html

The device uses OOK (ASK) encoding.
The device sends a transmission every 1 second when quick deflation is detected, every 13 - 23 sec when quick inflation is detected, and every 4 min 40 s under steady state pressure.
A transmission starts with a preamble of 0x0000 and the packet is sent twice.

S.a issue #2384, #2657, #2063, #2677

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
    // preamble is ffff
    uint8_t const preamble_pattern[] = {0xff, 0xff};

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }
    int pos = 0;
    bitbuffer_invert(bitbuffer);
    pos = bitbuffer_search(bitbuffer, 0, pos, preamble_pattern, sizeof(preamble_pattern) * 8);
    if (pos >= bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 3, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }
    if (pos + 8 * 8 > bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 2, __func__, "Length check fail");
        return DECODE_ABORT_LENGTH;
    }
    uint8_t b[7]  = {0};
    uint8_t cc[1] = {0};
    bitbuffer_extract_bytes(bitbuffer, 0, pos + 16, cc, sizeof(cc) * 8);
    bitbuffer_extract_bytes(bitbuffer, 0, pos + 24, b, sizeof(b) * 8);

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

    int temperature_C      = b[4] - 50;
    int flags1             = b[5];
    int flags2             = b[6];
    int fast_leak_detected = (flags1 & 0x10);      // fast leak - reports every second
    int infl_detected      = (flags1 & 0x20) >> 5; // inflating - reports every 15 - 20 sec

    int fast_leak      = fast_leak_detected && !infl_detected;
    float pressure_kPa = (((flags2 & 0x01) << 8) + b[3]) * 2.5;

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
        .name        = "EezTire E618, Carchet TPMS",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 50,
        .long_width  = 50,
        .reset_limit = 120,
        .decode_fn   = &tpms_eezrv_decode,
        .fields      = output_fields,
};
