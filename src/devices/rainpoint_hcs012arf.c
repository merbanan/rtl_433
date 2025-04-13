/** @file
    RainPoint HCS012ARF Rain Gauge sensor.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>
    Copyright (C) 2025 Bruno OCTAU (ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
RainPoint HCS012ARF Rain Gauge sensor.

Manufacturer:
- Fujian Baldr Technology Co., Ltd

RF Information:
- Seen on 433.92 Mhz.
- FCC ID : 2AWDBHCS008FRF
- RF Module used in other RainPoint / Fujian Baldr Technology Co., Ltd devices : HCS008FRF, HCS012ARF, HTV113FRF, HTV213FRF, TTC819, TCS008B

Description of the HCS012ARF Rain Gauge Sensor:
- Rainfall Range: 0-9999mm , but 2 bytes identified, missing 1 bit MSB somewhere in the data layout flags
- Accuracy: ±0.1mm
- Data Reporting: Every 3 mins

A Transmission contains ten packets with Manchester coded data, reflected

Data layout:

    Byte Index  0  1  2  3  4  5  6  7  8  9
    Sample     a5 08 54 03 04 61 03 00 00 c7 [Batt inserted]
               HH[II II II II FB FF RR RR]SS

- HH: {8} Header, fixed 0xa5 (or 0xa4 when MC Zero bit decoded)
- ID: {32} Sensor ID, does not change with new battery, looks unique
- FF: {6} Fixed value, 0x18, may contains 1 bit MSB Rain Gauge
- B:{1} Low Battery flag = 1, Good Battery = 0
- B:{1} Powered on, batteries are inserted = 1, then always = 0
- FF:{8} Fixed value, 0x03, may contains 1 bit MSB Rain Gauge
- RR:{16} little-endian rain gauge value, scale 10 (1 Tip = 0,1 mm), 2 bytes are not enough, max 0xFFFF = 6553.5 mm, 1 bit MSB somewhere in flags ?
- SS:{8} Byte sum of previous bytes except header [value in the hooks, from ID to Rain gauge].

Raw data:

    rtl_433 -X 'n=HCS012ARF,m=OOK_PCM,s=320,l=320,r=1000,g=700,repeats>=5,unique'


*/

static int rainpoint_hcs012arf_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{

    // Find repeats
    int row = bitbuffer_find_repeated_row(bitbuffer, 4, 163);
    if (row < 0)
        return DECODE_ABORT_EARLY;

    if (bitbuffer->bits_per_row[row] > 163)
        return DECODE_ABORT_LENGTH;

    bitbuffer_t msg = {0};
    bitbuffer_manchester_decode(bitbuffer, row, 0, &msg, 10 * 2 * 8); // including header
    bitbuffer_invert(&msg);

    uint8_t *b = msg.bb[0];
    reflect_bytes(b, 10);

    if (b[0] != 0xa5) // header = 0xa5, could be 0xa4 when MC Zero bit decoded
        return DECODE_ABORT_EARLY;

    decoder_log_bitrow(decoder, 2, __func__, b, 10 * 8, "MC and Reflect decoded");

    // Checksum
    int sum = add_bytes(&b[1], 8); // header not part of the sum
    if ((sum & 0xff) != b[9]) {
        decoder_logf(decoder, 2, __func__, "Checksum failed %04x vs %04x", b[9], sum);
        return DECODE_FAIL_MIC;
    }

    int id       = (b[4] << 24) | (b[3] << 16) | (b[2] << 8) | b[1]; // just a guess, little-endian
    int flags1   = b[5]; // may contains 1 bit MSB for Rain Gauge
    int bat_low  = (flags1 & 0x02) >> 1;
    //int bat_in   = (flags1 & 0x01); // power up, battery inserted = 1, then always 0
    int flags2   = b[6];        // may contains 1 bit MSB for Rain Gauge
    int rain_raw  = (b[8] << 8) | b[7]; // little-endian
    float rain_mm = rain_raw * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "RainPoint-HCS012ARF",
            "id",               "",                 DATA_INT,    id,     // decimal value reported at Rainpoint application
            "flags1",           "Flags 1",          DATA_FORMAT, "%02x", DATA_INT,  (flags1 >> 2), // remove battery flags
            "flags2",           "Flags 2",          DATA_FORMAT, "%02x", DATA_INT,  flags2,
            "battery_ok",       "Battery",          DATA_INT,    !bat_low,
            "rain_mm",          "Total rainfall",   DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_mm,
            "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "flags1",
        "flags2",
        "battery_ok",
        "rain_mm",
        "mic",
        NULL,
};

r_device const rainpoint_hcs012arf = {
        .name        = "RainPoint HCS012ARF Rain Gauge sensor",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 320,
        .long_width  = 320,
        .reset_limit = 1000,
        .gap_limit   = 700,
        .decode_fn   = &rainpoint_hcs012arf_decode,
        .fields      = output_fields,
};
