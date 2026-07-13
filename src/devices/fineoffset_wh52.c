/** @file
    Fine Offset Electronics / Ecowitt WH52 3-in-1 Soil Moisture / Temperature / EC sensor.

    Copyright (C) 2026 Andreas Braunlich <andreasbraunlich@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Fine Offset Electronics / Ecowitt WH52 3-in-1 Soil Moisture / Temperature / EC sensor.

The WH52 is a wireless soil probe that reports volumetric soil moisture (%),
soil temperature and electrical conductivity (EC, uS/cm). It transmits on the
license-free ISM band (915 MHz in the US, 868 MHz EU, 433.92 MHz elsewhere)
approximately every 70 seconds. It is a member of the Fine Offset family and
shares the WH51's FSK/PCM modulation (58 us bit width, NRZ) and preamble.

Reverse-engineered 2026-07-11 by capturing live frames with a flex decoder
(`-X 'n=wh52,m=FSK_PCM,s=58,l=58,r=5000,preamble=aa2dd4'`) alongside the
manufacturer's gateway/app readouts, and by controlled calibration:
- Temperature verified against the app to +/-0.1 F across 18-25 C.
- Moisture verified 0% (air) and 100% (submerged), exact.
- EC calibrated with a stepped salt-water series (11 points, 340-7430 uS/cm,
  spanning two 16-bit overflow boundaries); the linear fit below is within ~1%.

Data layout:

Preamble: aa aa aa 2d d4

24-byte payload (indices after the preamble):

    Byte   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
           A2 II II II Bt tt MM rr Ec cc rr Rr RR RR RR RR RR RR RR RR RR RR YY SS

- Byte 0: family / type = 0xA2 (WH52 signature)
- Byte 1..3: device ID (24-bit, hex)
- Byte 4: bits 7..5 = transmission boost/retry counter; bits 4..0 = temperature high 5 bits
- Byte 5: temperature low 8 bits; temperature_C = (((b4 & 0x1F) << 8) | b5) * 0.1 - 40.0
- Byte 6: soil moisture (%), 0x00..0x64
- Byte 7: moisture raw / gain (not fully characterised)
- Byte 8: bits 7..4 = moisture-raw high nibble (not characterised); bits 3..0 = EC high nibble (bits 16..19 of the 20-bit EC value)
- Byte 9: EC bits 15..8
- Byte 10: EC bits 7..0; ec_raw = ((b8 & 0x0F) << 16) | (b9 << 8) | b10 (20-bit); conductivity_uS = ec_raw / 25.6 (empirical; 25.6 = 256/10)
- Byte 11: coarse EC / range indicator (redundant, low nibble fixed 0x6)
- Byte 15: battery voltage; volts ~= b15 * 0.02 - 0.06 (empirical, fit from 4 field units reading 1.56/1.58/1.58/1.62 V vs bytes 0x51/0x52/0x52/0x54; scale approximate pending a wider range)
- Bytes 12-14, 16-21: per-unit fixed data (factory serial / calibration), constant per unit, differ between units. Not decoded.
- Byte 22: CRC-8, poly 0x31, init 0x00, over bytes 0..21
- Byte 23: checksum = sum(bytes 0..22) & 0xFF

Format string: FF II II II Bt tt MM rr Ec cc rr Rr RR RR RR RR RR RR RR RR RR RR CC SS

The WH51 decoder ignores these frames (its family-byte check b[0] != 0x51 fails),
so adding this decoder does not conflict with WH51 support.
*/

static int fineoffset_wh52_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xAA, 0x2D, 0xD4};
    uint8_t b[24];
    unsigned bit_offset;

    // 24-byte payload = 192 bits, plus the sync; require a plausible row length
    if (bitbuffer->bits_per_row[0] < 200) {
        return DECODE_ABORT_LENGTH;
    }

    bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8;
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) {
        decoder_logf_bitbuffer(decoder, 1, __func__, bitbuffer, "short package. Header index: %u", bit_offset);
        return DECODE_ABORT_LENGTH;
    }
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    if (b[0] != 0xa2) {
        decoder_logf(decoder, 1, __func__, "Msg family unknown: 0x%02x", b[0]);
        return DECODE_ABORT_EARLY;
    }

    // Byte 23 is a running sum of bytes 0..22
    if ((add_bytes(b, 23) & 0xff) != b[23]) {
        decoder_log_bitrow(decoder, 1, __func__, b, sizeof(b) * 8, "Checksum error");
        return DECODE_FAIL_MIC;
    }
    // Byte 22 is a CRC-8 (poly 0x31, init 0x00) over bytes 0..21
    if (crc8(b, 22, 0x31, 0) != b[22]) {
        decoder_log_bitrow(decoder, 1, __func__, b, sizeof(b) * 8, "CRC error");
        return DECODE_FAIL_MIC;
    }

    char id[7];
    snprintf(id, sizeof(id), "%02x%02x%02x", b[1], b[2], b[3]);

    int boost       = (b[4] & 0xe0) >> 5;
    int temp_raw    = ((b[4] & 0x1f) << 8) | b[5];
    float temp_c    = temp_raw * 0.1f - 40.0f;
    int moisture    = b[6];
    int ec_raw      = ((b[8] & 0x0f) << 16) | (b[9] << 8) | b[10];
    float ec_uscm   = ec_raw / 25.6f;      // empirical calibration, see notes above
    float battery_v = b[15] * 0.02f - 0.06f; // empirical, see notes above

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                     DATA_STRING, "Fineoffset-WH52",
            "id",               "ID",                   DATA_STRING, id,
            "temperature_C",    "Temperature",          DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "moisture",         "Moisture",             DATA_FORMAT, "%u %%",  DATA_INT,    moisture,
            "conductivity",     "Conductivity",         DATA_FORMAT, "%.0f uS/cm", DATA_DOUBLE, ec_uscm,
            "battery_V",        "Battery Voltage",      DATA_FORMAT, "%.2f V",     DATA_DOUBLE, battery_v,
            "boost",            "Transmission boost",   DATA_INT,    boost,
            "mic",              "Integrity",            DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields_wh52[] = {
        "model",
        "id",
        "temperature_C",
        "moisture",
        "conductivity",
        "battery_V",
        "boost",
        "mic",
        NULL,
};

r_device const fineoffset_wh52 = {
        .name        = "Fine Offset Electronics / Ecowitt WH52 Soil Moisture/Temperature/EC Sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 5000,
        .decode_fn   = &fineoffset_wh52_decode,
        .fields      = output_fields_wh52,
};
