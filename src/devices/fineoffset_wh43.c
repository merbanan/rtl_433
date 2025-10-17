/** @file
    Fine Offset Electronics WH43 air quality sensor.

    Analysis by \@andrewjmcginnis
    Copyright (C) 2025 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Fine Offset Electronics WH43 air quality sensor.

S.a. the draft in #3179

The sensor sends a data burst every 10 minutes.  The bits are PCM
modulated with Frequency Shift Keying.

Ecowitt advertises this device as a PM2.5 sensor.  It contains a
Honeywell PM2.5 sensor:

https://sensing.honeywell.com/honeywell-sensing-particulate-hpm-series-datasheet-32322550.pdf

However, the Honeywell datasheet says that it also has a PM10 output
which is "calculated from" the PM2.5 reading.  While there is an
accuracy spec for PM2.5, there is no specification of an kind from
PM10.  The datasheet does not explain the calculation, and does not
give references to papers in the scientific literature.

Note that PM2.5 is the mass of particles <= 2.5 microns in 1 m^3 of
air, and PM10 is the mass of particles <= 10 microns.  Therefore the
difference in those measurements is the mass of particles > 2.5
microns and <= 10 microns, sometimes called PM2.5-10.  By definition
these particles are not included in the PM2.5 measurement, so
"calculating" doesn't make sense.  Rather, this appears an assumption
about correlation, meaning how much mass of larger particles is likely
to be present based on the mass of the smaller particles.

The serial stream from the sensor has fields for PM2.5 and PM10 and
these fields have been verified to appear in the transmitted signal by
cross-comparing the internal serial lines and data received via
rtl_433.

The Ecowitt displays show only PM2.5, and Ecowitt confirmed that the
second field is the PM10 output of the sensor but said the value is
not accurate so they have not adopted it.

By observation of an Ecowitt WH41, the formula is pm10 = pm2.5 +
increment(pm2.5), where the increment is by ranges from the following
table (with gaps when no samples have been observed).  It is left as
future work to compare with an actual PM10 sensor.

    0 to 24     | 1
    25 to 106   | 2
    109 to 185  | 3
    190 to 222  | 4
    311         | 5
    390         | 6

This code is similar to the Fine Offset/Ecowitt WH0290/WH41/PM25 devices.
The WH43 uses a longer packet, due in part to the 24-bit ID (vs 8-bit for the WH41),
which then offsets the location of the battery, PM2.5/10, CRC, and Checksum bits.

Data layout:
    aa 2d d4 43 cc cc cc 41 9a 41 ae c1 99 9
             FF II II II ?P PP ?A AA CC BB

- F: 8 bit Family Code?
- I: 8 bit device id (corresponds to sticker on device in hex)
- ?: 1 bit?
- b: 1 bit MSB of battery bars out of 5
- P: 14 bit PM2.5 reading in ug/m3
- b: 2 bits LSBs of battery bars out of 5
- A: 14 bit PM10.0 reading in ug/m3
- C: 8 bit CRC checksum of the previous 6 bytes
- B: 8 bit Bitsum (sum without carry, XOR) of the previous 7 bytes

Preamble: aa2dd4
    FAM:8d ID: 24h 1b Bat_MSB:1d PMTWO:14d Bat_LSB:2d PMTEN:14d CRC:8h SUM:8h bbbbb
*/
static int fineoffset_wh43_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xAA, 0x2D, 0xD4};

    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8;
    uint8_t b[10];
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        decoder_logf_bitbuffer(decoder, 1, __func__, bitbuffer, "short package. Row length: %u. Header index: %u", bitbuffer->bits_per_row[0], bit_offset);
        return DECODE_ABORT_LENGTH;
    }
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    // Check first byte for our type code
    if (b[0] != 0x43) {
        decoder_logf(decoder, 1, __func__, "Not our device type: %02x", b[0]);
        return DECODE_ABORT_EARLY;
    }

    decoder_log_bitrow(decoder, 2, __func__, b, sizeof (b) * 8, "Payload data");

    int crc = crc8(b, 8, 0x31, 0x00);  // Compute CRC over first 8 bytes.
    int sum = add_bytes(b, 9) & 0xff;
    if (crc != b[8] || sum != b[9]) {
        decoder_logf(decoder, 1, __func__, "Checksum error: %02x %02x", crc, sum);
        return DECODE_FAIL_MIC;
    }

    // int family     = b[0];
    int id         = (b[1] << 16) | (b[2] << 8) | b[3]; // 24-bit ID
    int pm25       = ((b[4] & 0x3F) << 8) | b[5];
    int pm100      = ((b[6] & 0x3F) << 8) | b[7];
    int batt_bars  = ((b[4] & 0x40) >> 4) | ((b[6] & 0xC0) >> 6);
    int ext_power  = batt_bars == 6;
    float batt_lvl = MIN(batt_bars * 0.2f, 1.0f); // cap for external power

    /* clang-format off */
    data_t *data = data_make(
            "model",                    "",                         DATA_STRING, "Fineoffset-WH43",
            "id",                       "ID",                       DATA_FORMAT, "%06x", DATA_INT, id,
            "battery_ok",               "Battery",      	        DATA_INT,    batt_bars > 1, // Level 1 means "Low"
    		"battery_pct",              "Battery level",            DATA_INT,    100 * batt_lvl, // Note: this might change with #3103
            "ext_power",                "External Power",           DATA_INT,    ext_power,
            "pm2_5_ug_m3",              "2.5um Fine PM",            DATA_FORMAT, "%d ug/m3", DATA_INT, pm25 / 10,
            "estimated_pm10_0_ug_m3",   "Estimate of 10um Coarse PM", DATA_FORMAT, "%d ug/m3", DATA_INT, pm100 / 10,
            "mic",                      "Integrity",                DATA_STRING, "CRC",
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
        "ext_power",
        "pm2_5_ug_m3",
        "estimated_pm10_0_ug_m3",
        "mic",
        NULL,
};

r_device const fineoffset_wh43 = {
        .name        = "Fine Offset Electronics WH43 air quality sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 2500,
        .decode_fn   = &fineoffset_wh43_decode,
        .fields      = output_fields,
};
