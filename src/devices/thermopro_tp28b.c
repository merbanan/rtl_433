/** @file
    ThermoPro TP28b Super Long Range Wireless Meat Thermometer for Smoker BBQ Grill.

    Copyright (C) 2024 Josh Pearce <joshua.pearce@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int thermopro_tp28b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
ThermoPro TP28b Super Long Range Wireless Meat Thermometer for Smoker BBQ Grill.

Example data:

    rtl_433 -f 915M -F json -X "n=tp28b,m=FSK_PCM,s=105,l=105,r=5500,preamble=d2aa2dd4" | jq --unbuffered -r '.codes[0]'
    (spaces below added manually)

    {259}2802 0626 0000 2802 1107 0000 a290 6d70 a702 000000000000 aaaa 0000000000000 [over1: 261C, Temp1: 23C, over2: 71C, Temp2: 23C]
    {259}2217 0626 0000 3102 1107 0000 a290 6d70 bf02 000000000000 aaaa 0000000000000 [over1: 261C, Temp1: 172, over2: 71C, Temp2: 23C]
    {259}4421 1026 9009 3002 1012 4410 a298 6d70 5a03 000000000000 aaaa 0000000000000 [over1: 261C, Temp1: 214, over2: 121C, Temp2: 23C]

Data layout:

    [p1_temp] [p1_set_hi] [p1_set_lo] [p2_temp] [p2_set_hi] [p2_set_lo] [flags] [id] [cksum] 000000000000 aaaa 0000000000000

- p1_temp: probe 1 current temp. 16 bit BCD
- p1_set_hi: probe 1 high alarm temp. 16 bit BCD
- p1_set_lo: probe 1 low alarm temp. 16 bit BCD
- p2_temp: probe 2 current temp. 16 bit BCD
- p2_set_hi: probe 2 high alarm temp. 16 bit BCD
- p2_set_lo: probe 2 low alarm temp. 16 bit BCD
- flags: 16 bit status flags
- id: 16 bit identifier
- cksum: 16 bit checksum

Bit bench format:

    A_TEMP:hhhh A_HI:hhhh A_LO:hhhh B_TEMP:hhhh B_HI:hhhh B_LO:hhhh FLAGS:hhhh ID:hhhh CHK:hhhh hhhhhhhhhhhh hhhh hhhhhhhhhhhhh

Temps are little-endian 16 bit Binary Coded Decimals (BCD), LLHH. They are always in Celsius.

    Example: 2821,
        28 => 2.8 deg C
        21 => 210 deg C
        210 + 2.8 = 212.8 C (displayed rounded to 213)

Below is some work on status/alarm flags, but I can't quite make sense of them all:

    02d8 => F,  p1: in-range,    p2: in-range
    02f9 => F,  p1: low,         p2: in-range
    02dd => F,  p1: in-range,    p2: low
    02de => F,  p1: in-range,    p2: hi
    02fa => F,  p1: hi,          p2: in-range
    86f9 => F,  p1: low,         p2: low
    82f9 => F,  p1: low,         p2: low        ack'd
    a2f9 => C,  p1: low,         p2: low        ack'd
    a6f9 => C,  p1: low,         p2: low        unack'd

- flags & 0x2000 => Display in Celcius, otherwise Fahrenheit
- flags & 0x0400 => Alarm unacknowledged, otherwise acknowledged
- flags & 0x0020 => P1 in alarm, otherwise normal
- flags & 0x0004 => P2 in alarm, otherwise normal
- flags & 0x0001 => P2 in alarm low
*/

// Convert BCD encoded temp to float
static float bcd2float(uint8_t lo, uint8_t hi)
{
    return ((hi & 0xF0) >> 4) * 100.0f + (hi & 0x0F) * 10.0f + ((lo & 0xF0) >> 4) * 1.0f + (lo & 0x0F) * 0.1f;
}

static int thermopro_tp28b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xd2, 0xaa, 0x2d, 0xd4};

    uint8_t b[18];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    int msg_len = bitbuffer->bits_per_row[0];
    if (msg_len < 240) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }
    if (msg_len > 451) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    int offset = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= msg_len) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 18 * 8);

    int sum = (add_bytes(b, 16) & 0xff) - b[16];
    if (sum) {
        decoder_log_bitrow(decoder, 1, __func__, b, sizeof(b) * 8, "Checksum error");
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 2, __func__, b, bitbuffer->bits_per_row[0] - offset, "");

    uint16_t id     = b[15] | b[14] << 8;
    uint16_t flags  = b[13] | b[12] << 8;
    float p1_temp   = bcd2float(b[0], b[1]);
    float p1_set_hi = bcd2float(b[2], b[3]);
    float p1_set_lo = bcd2float(b[4], b[5]);
    float p2_temp   = bcd2float(b[6], b[7]);
    float p2_set_hi = bcd2float(b[8], b[9]);
    float p2_set_lo = bcd2float(b[10], b[11]);

    /* clang-format off */
    data_t *data = data_make(
            "model",                "",                             DATA_STRING,    "ThermoPro-TP28b",
            "id",                   "",                             DATA_FORMAT,    "%04x",   DATA_INT,    id,
            "temperature_1_C",      "Temperature 1",                DATA_FORMAT,    "%.1f C", DATA_DOUBLE, p1_temp,
            "alarm_high_1_C",       "Temperature 1 alarm high",     DATA_FORMAT,    "%.1f C", DATA_DOUBLE, p1_set_hi,
            "alarm_low_1_C",        "Temperature 1 alarm low",      DATA_FORMAT,    "%.1f C", DATA_DOUBLE, p1_set_lo,
            "temperature_2_C",      "Temperature 2",                DATA_FORMAT,    "%.1f C", DATA_DOUBLE, p2_temp,
            "alarm_high_2_C",       "Temperature 2 alarm high",     DATA_FORMAT,    "%.1f C", DATA_DOUBLE, p2_set_hi,
            "alarm_low_2_C",        "Temperature 2 alarm low",      DATA_FORMAT,    "%.1f C", DATA_DOUBLE, p2_set_lo,
            "flags",                "Status flags",                 DATA_FORMAT,    "%04x",   DATA_INT,    flags,
            "mic",                  "Integrity",                    DATA_STRING,    "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_1_C",
        "alarm_high_1_C",
        "alarm_low_1_C",
        "temperature_2_C",
        "alarm_high_2_C",
        "alarm_low_2_C",
        "flags",
        "mic",
        NULL,
};

r_device const thermopro_tp28b = {
        .name        = "ThermoPro TP28b Super Long Range Wireless Meat Thermometer for Smoker BBQ Grill",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 105,
        .long_width  = 105,
        .reset_limit = 5500,
        .decode_fn   = &thermopro_tp28b_decode,
        .fields      = output_fields,
};
