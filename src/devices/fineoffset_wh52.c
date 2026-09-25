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
- Moisture verified against the app at 0% (air) and 100% (submerged). Note that
  both ends are clamped: the raw measurement keeps moving after the percentage has
  stopped, which is why moisture_raw is reported separately.
- EC calibrated with a stepped salt-water series (11 points, 340-7430 uS/cm,
  spanning two 16-bit overflow boundaries); the linear fit below is within ~1%.

The battery decode at byte 20 is not from that work. It was corrected by vgabor99
in PR #3668, which also named the conductivity field; the original submission had
placed battery voltage at byte 15.

Bytes 7, 8 and 11 were characterised in 2026-09 with a stepped salt-water series
and raw IQ captures covering every value the 20-bit EC carry can take. Everything
here is from 915 MHz units: eight of them for the per-byte observations, four of
those for the per-unit moisture figures, which should be read as examples of the
spread rather than as bounds. Method, data, and the
conclusions we published and later withdrew:
https://github.com/Verstreubulator/wh52-characterization

Data layout:

Preamble: aa aa aa 2d d4

24-byte payload (indices after the preamble):

    Byte   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
           A2 II II II Bt tt MM rr rc cc cc Rr RR RR RR RR RR RR RR RR BB RR YY SS

- Byte 0: family / type = 0xA2 (WH52 signature)
- Byte 1..3: device ID (24-bit, hex)
- Byte 4: bits 7..5 = transmission boost/retry counter; bits 4..0 = temperature high 5 bits
- Byte 5: temperature low 8 bits; temperature_C = (((b4 & 0x1F) << 8) | b5) * 0.1 - 40.0
- Byte 6: soil moisture (%), 0x00..0x64
- Byte 7: raw moisture measurement, low 8 bits
- Byte 8: bits 7..4 = raw moisture high 4 bits; bits 3..0 = EC high nibble (bits 16..19 of the 20-bit EC value).
  moisture_raw = ((b8 & 0xF0) << 4) | b7 (12-bit). This byte carries two unrelated fields at once.
  moisture_raw is linear in the reported percentage, but with a slope and offset that differ per unit
  (9.86-10.37 counts per 1%, zero point 607-640 across 4 units), so raw values are not comparable
  between sensors without calibrating each one. The WH51 exposes the equivalent field as ad_raw;
  it is 9-bit there with endpoints near 70 and 450, so the two are not on the same scale.
- Byte 9: EC bits 15..8
- Byte 10: EC bits 7..0; ec_raw = ((b8 & 0x0F) << 16) | (b9 << 8) | b10 (20-bit); conductivity_uS = ec_raw / 25.6 (empirical; 25.6 = 256/10)
  The sensor clamps conductivity at about 10000 uS/cm (we recorded 10001 to 10008) while the
  20-bit field has room for roughly four times that. Whether the limit is the sensing element or the firmware we cannot say.
- Byte 11: bits 7..4 = EC auto-ranging gain indicator; bits 3..0 = 0x6 in every CRC-valid frame we
  have. ec_range = b11 >> 4. Ten distinct values were seen between 5 and 10000 uS/cm.
  It is not monotone in the reported value, and that is the reason to expose it: the same sensor
  reported 4870 uS/cm in range 6 and 4700 in range 7, so two readings showing the same conductivity
  can come from different gain states and nothing else in the frame says which.
  Between consecutive transmissions the indicator moves in the same direction as the reported
  conductivity in 100 of the 101 changes we logged; the exception is at the clamped ceiling, where
  it fell from 13 to 12 while the reported value rose. That series was logged with checksum
  validation only, and one frame is excluded from those counts: it decoded to 89.4 degC with this
  byte's low nibble at 0xc. Including it gives 102 of 103. The raw captures we published are
  CRC-checked but contain ranges 1, 4, 5, 7 and 13 with no transitions between them.
  It does not rescale the EC value. The switching thresholds are uneven when expressed as reported
  conductivity and no rule has been established.
  Do not use this byte as a validity check; it is not constant.
- Byte 20: battery voltage; battery_mV ~= b20 * 20 (0.02 V/LSB, no offset; empirical). Reported as battery_ok + battery_mV.
- Bytes 12, 13, 15, 16, 17, 19: per-unit fixed data (factory serial / calibration), constant per unit,
  differ between units. Not decoded.
- Byte 14: 0x93 on all 8 units seen. Constant, NOT per-unit.
- Byte 18: 0x7b on 7 of 8 units, 0x8c on the eighth. Appears per-unit.
- Byte 21: 0x08 on units with IDs beginning 0x005, 0x09 on those beginning 0x007. Probably a batch or
  revision marker. All observations are from 915 MHz units; 868 MHz behaviour is unknown.
- Byte 22: CRC-8, poly 0x31, init 0x00, over bytes 0..21
- Byte 23: checksum = sum(bytes 0..22) & 0xFF

Format string:

    FF II II II Bt tt MM rr rc cc cc Rr RR RR RR RR RR RR RR RR BB RR YY SS

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

    int boost      = (b[4] & 0xe0) >> 5;
    int temp_raw   = ((b[4] & 0x1f) << 8) | b[5];
    float temp_c   = temp_raw * 0.1f - 40.0f;
    int moisture   = b[6];
    int moist_raw  = ((b[8] & 0xf0) << 4) | b[7]; // 12-bit, per-unit scale (see notes above)
    int ec_raw     = ((b[8] & 0x0f) << 16) | (b[9] << 8) | b[10];
    float ec_uscm  = ec_raw / 25.6f; // empirical calibration, see notes above
    int ec_range   = b[11] >> 4;     // auto-ranging gain indicator, see notes above
    int battery_mv = b[20] * 20;     // 0.02 V/LSB, no offset (empirical, see notes above)

    // Battery reported as a percentage of the usable ~1.3-1.6 V range
    // (WH51 sibling breakpoints), following the standard battery convention.
    float batt_lvl = (battery_mv - 1300) * (1.0f / 300.0f);
    if (batt_lvl < 0.0f) {
        batt_lvl = 0.0f;
    }
    if (batt_lvl > 1.0f) {
        batt_lvl = 1.0f;
    }
    /* clang-format off */
    data_t *data = data_make(
            "model",                "",                     DATA_STRING, "Fineoffset-WH52",
            "id",                   "ID",                   DATA_STRING, id,
            "temperature_C",        "Temperature",          DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "moisture",             "Moisture",             DATA_FORMAT, "%u %%",  DATA_INT,    moisture,
            "moisture_raw",         "Moisture raw",         DATA_INT,    moist_raw,
            "conductivity_uS_cm",   "Conductivity",         DATA_FORMAT, "%.0f uS/cm", DATA_DOUBLE, ec_uscm,
            "ec_range",             "EC range",             DATA_INT,    ec_range,
            "battery_ok",           "Battery level",        DATA_DOUBLE, batt_lvl,
            "battery_mV",           "Battery",              DATA_FORMAT, "%d mV",      DATA_INT,    battery_mv,
            "boost",                "Transmission boost",   DATA_INT,    boost,
            "mic",                  "Integrity",            DATA_STRING, "CRC",
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
        "moisture_raw",
        "conductivity_uS_cm",
        "ec_range",
        "battery_ok",
        "battery_mV",
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
