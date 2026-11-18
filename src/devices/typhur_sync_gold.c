/** @file
    Typhur and subsidiary ThermoMaven meat thermometer probes.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"
#include <math.h>

/**
Typhur Sync Gold and ThermoMaven WTP series meat thermometer probes.

Typhur reverse engineered in issue #3138 by \@hakong and \@kevin-david, with
guidance from \@zuckschwerdt. Further extended to ThermoMaven WTP in
context of #3627 by \@sfowler w/ additional contribution from \@dandwyer.
FSK_PCM at 12.5 us/bit, long 0xaa preamble, 16 bit sync word `0x5754`,
24 byte payload:

    ID:24h ?:8h STATUS:8h VARIANT:8h T1:16h T2:16h T3:16h T4:16h T5:16h
    AMBIENT:16h BATTERY:16h COUNTER:16h CRC:16h

- ID: 24 bit, one per physical probe
- STATUS: bit 3 set when the probe is seated in its charging base
- VARIANT (byte 5): model/firmware tag, constant per model.
    - 0x57 = Typhur Sync Gold
    - 0x1d = 915 MHz ThermoMaven WTP3000 used on G1 / WT10 base stations
    - 0x22 = 916 MHz ThermoMaven WTP4000 used on G2 / WT07 base stations
- T1-T5: probe temperature sensors, little-endian
- AMBIENT: little-endian, separate thermistor
- BATTERY: little-endian; scale is per-make
- COUNTER: little-endian, increments every transmission
- CRC: CRC-16 poly 0x8005 init 0x0000 over the preceding 22 bytes

Temperature encoding differs by make/model, selected by the byte-5 variant
tag (constant per model, distinct between models). The Typhur encodes
temperature directly on a linear scale while the ThermoMaven encodes raw
ADC codes of NTC thermistors, which must be converted to degrees Celsius via
Steinhart-Hart equation.
*/

#define TYPHUR_SYNC_GOLD_PAYLOAD_LEN 24

typedef enum {
    WTP_INTERNAL,    // Used for the 5 internal probe temperature sensors
    WTP_AMBIENT_LOW, // Used when ambient temperature is below ~120C
    WTP_AMBIENT_HIGH // Used when ambient temperature is above ~120C.
} thermomaven_wtp_channel_variant_t;

static uint8_t const typhur_sync_gold_sync[2] = {0x57, 0x54};

static int is_typhur_variant(uint8_t variant)
{
    return variant == 0x57;
}

/// Convert a raw NTC ADC code to degrees Celsius via divider + Steinhart-Hart.
static float thermomaven_wtp_steinhart(uint16_t adc_counts, float n, float a, float b, float c, float d)
{
    // ThermoMaven G1 / WT10 (variant 0x1d): each 16-bit field is the raw ADC code
    // of an NTC thermistor read through a voltage divider, not a scaled
    // temperature (the code decreases as temperature rises). Recover the
    // resistance as R = adc_counts / (N - adc_counts) (the series resistor folds into
    // the constants) and apply Steinhart-Hart:
    //      1/T[K] = A + B*ln(R) + C*ln(R)^2 + D*ln(R)^3

    // Guard the divider/log domain: code must stay within (0, N).
    if (adc_counts < 1) {
        adc_counts = 1;
    }
    if ((float)adc_counts > n - 1.0f) {
        adc_counts = (uint16_t)(n - 1.0f);
    }
    float lr  = logf((float)adc_counts / (n - (float)adc_counts));
    float t_k = 1.0f / (a + b * lr + c * lr * lr + d * lr * lr * lr);
    return t_k - 273.15f;
}

/// Convert a raw field to degrees Celsius using the variant's model.
static float thermomaven_wtp_temp_c(uint16_t adc_counts, thermomaven_wtp_channel_variant_t variant)
{
    // Each of the variants defined below has a different ADC to temperature transfer function,
    // which is defined by empircally derived Steinhart-Hart coefficients.
    switch (variant) {
    case WTP_INTERNAL: return thermomaven_wtp_steinhart(adc_counts, 42784.11f, 3.38191203e-03f, 3.15996392e-04f, 1.28546405e-05f, 1.88934100e-06f);
    case WTP_AMBIENT_LOW: return thermomaven_wtp_steinhart(adc_counts, 50257.07f, 3.29390756e-03f, 3.48641617e-04f, 7.82776569e-05f, 1.80483142e-05f);
    case WTP_AMBIENT_HIGH: return thermomaven_wtp_steinhart(adc_counts, 38194.51f, 2.02454117e-03f, 2.17752569e-04f, -3.11401505e-06f, -3.26078685e-06f);
    }
    return NAN; // Unreachable: the enum is fully covered.
}

/// Convert a raw field to battery voltage
static float thermomaven_wtp_battery_voltage(uint16_t adc_counts)
{
    // From https://fccid.io/2BEFM-WTP4000/Internal-Photos/Internal-Photos-8235048, we see:
    // * Battery is HTC4016 w/ nominal voltage of 2.4V
    // * MCU is TI MSPM0C1104S w/ either of 1.4V or 2.5V reference voltage (VREF) for the ADC.
    // Given 2.5V is too high for a 2.4V battery, we assume the 1.4V reference is used.
    // The app and base stations to not disclose the battery voltage, but a 140kOhm / 100kOhm
    // voltage divider is presumed as it yields logically scaled battery voltage readings.
    static float const adc_ref_volts   = 1.4f;
    static float const adc_full_scale  = 1024.0f; // 10-bit ADC
    static float const div_r1_kohm     = 140.0f;
    static float const div_r2_kohm     = 100.0f;
    static float const volts_per_count = (adc_ref_volts / adc_full_scale) * ((div_r1_kohm + div_r2_kohm) / div_r2_kohm);
    return adc_counts * volts_per_count;
}

static int typhur_payload_decode(r_device *decoder, bitbuffer_t *bitbuffer, uint8_t b[TYPHUR_SYNC_GOLD_PAYLOAD_LEN])
{
    for (int row = 0; row < bitbuffer->num_rows; row++) {
        int pos = bitbuffer_search(bitbuffer, row, 0, typhur_sync_gold_sync, 16);
        if (pos >= bitbuffer->bits_per_row[row]) {
            continue;
        }
        pos += 16;

        if (bitbuffer->bits_per_row[row] - pos < TYPHUR_SYNC_GOLD_PAYLOAD_LEN * 8) {
            continue;
        }

        bitbuffer_extract_bytes(bitbuffer, row, pos, b, TYPHUR_SYNC_GOLD_PAYLOAD_LEN * 8);

        uint16_t crc      = crc16(b, 22, 0x8005, 0x0000);
        uint16_t crc_recv = (b[22] << 8) | b[23];
        if (crc != crc_recv) {
            decoder_logf(decoder, 1, __func__, "CRC invalid %04x != %04x", crc, crc_recv);
            continue;
        }
        return 1;
    }
    return DECODE_FAIL_MIC;
}

/**
Typhur Sync Gold decode.

Shares the frame layout documented at the top of this file. Temperatures are
encoded on a linear scale: T1-T5 in centi-C, ambient in deci-C, battery in
centi-V. Frames carry the `0x57` variant tag.

@sa thermomaven_wtp_decode()
*/
static int typhur_sync_gold_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t b[TYPHUR_SYNC_GOLD_PAYLOAD_LEN];
    int rtn = typhur_payload_decode(decoder, bitbuffer, b);
    if (rtn > 0) {
        uint8_t variant = b[5];
        if (!is_typhur_variant(variant)) {
            decoder_logf(decoder, 1, __func__, "Ignoring Thermomaven Model Variant: %02x", variant);
            return DECODE_FAIL_MIC;
        }

        uint32_t id     = ((uint32_t)b[0] << 16) | (b[1] << 8) | b[2];
        int in_base     = (b[4] & 0x08) != 0;
        float temp_1_c  = (b[6] | (b[7] << 8)) * 0.01f;
        float temp_2_c  = (b[8] | (b[9] << 8)) * 0.01f;
        float temp_3_c  = (b[10] | (b[11] << 8)) * 0.01f;
        float temp_4_c  = (b[12] | (b[13] << 8)) * 0.01f;
        float temp_5_c  = (b[14] | (b[15] << 8)) * 0.01f;
        float ambient_c = (b[16] | (b[17] << 8)) * 0.1f;
        float battery_v = (b[18] | (b[19] << 8)) * 0.01f;
        int counter     = b[20] | (b[21] << 8);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, "Typhur-SyncGold",
                "id",           "",             DATA_FORMAT, "%06x", DATA_INT, id,
                "in_base",      "In base",      DATA_INT,    in_base,
                "counter",      "Counter",      DATA_INT,    counter,
                "battery_V",    "Battery",      DATA_FORMAT, "%.2f V", DATA_DOUBLE, battery_v,
                "temperature_1_C", "Probe 1",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_1_c,
                "temperature_2_C", "Probe 2",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_2_c,
                "temperature_3_C", "Probe 3",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_3_c,
                "temperature_4_C", "Probe 4",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_4_c,
                "temperature_5_C", "Probe 5",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_5_c,
                "ambient_C",    "Ambient",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, ambient_c,
                "mic",          "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return DECODE_FAIL_MIC;
}

/**
ThermoMaven WTP decode.

Shares the frame layout documented at the top of this file, but T1-T5 and
AMBIENT are raw NTC ADC codes converted via Steinhart-Hart (see
thermomaven_wtp_temp_c()), and the battery field is an ADC code plus an
ambient gain-stage flag. Known variants are `0x1d` (WTP3000, G1/WT10) and
`0x22` (WTP4000, G2/WT07); `0x57` frames belong to the Typhur decoder above.

@sa typhur_sync_gold_decode()
*/
static int thermomaven_wtp_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t b[TYPHUR_SYNC_GOLD_PAYLOAD_LEN];
    int rtn = typhur_payload_decode(decoder, bitbuffer, b);
    if (rtn > 0) {
        uint8_t variant = b[5];
        if (is_typhur_variant(variant)) {
            decoder_logf(decoder, 1, __func__, "Ignoring Typhur Model Variant: %02x", variant);
            return DECODE_FAIL_MIC;
        }

        uint32_t id    = ((uint32_t)b[0] << 16) | (b[1] << 8) | b[2];
        int in_base    = (b[4] & 0x08) != 0;
        float temp_1_c = thermomaven_wtp_temp_c(b[6] | (b[7] << 8), WTP_INTERNAL);
        float temp_2_c = thermomaven_wtp_temp_c(b[8] | (b[9] << 8), WTP_INTERNAL);
        float temp_3_c = thermomaven_wtp_temp_c(b[10] | (b[11] << 8), WTP_INTERNAL);
        float temp_4_c = thermomaven_wtp_temp_c(b[12] | (b[13] << 8), WTP_INTERNAL);
        float temp_5_c = thermomaven_wtp_temp_c(b[14] | (b[15] << 8), WTP_INTERNAL);

        // The battery ADC value uses 10-bits out of the 16-bit field. The
        // preceding 11th bit is a flag that indicates which of two gain
        // stages is used for the ambient temperature sensor.
        uint16_t battery_adc_counts                       = (b[18] | (b[19] << 8));
        thermomaven_wtp_channel_variant_t ambient_variant = (battery_adc_counts & 0x0800) ? WTP_AMBIENT_HIGH : WTP_AMBIENT_LOW;
        battery_adc_counts &= 0x07ff; // mask off the flag bit(s), leaving the 10-bit ADC value
        float ambient_c = thermomaven_wtp_temp_c(b[16] | (b[17] << 8), ambient_variant);
        float battery_v = thermomaven_wtp_battery_voltage(battery_adc_counts);
        int counter     = b[20] | (b[21] << 8);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, "ThermoMaven-WTP",
                "id",           "",             DATA_FORMAT, "%06x", DATA_INT, id,
                "in_base",      "In base",      DATA_INT,    in_base,
                "counter",      "Counter",      DATA_INT,    counter,
                "battery_V",    "Battery",      DATA_FORMAT, "%.2f V", DATA_DOUBLE, battery_v,
                "temperature_1_C", "Probe 1",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_1_c,
                "temperature_2_C", "Probe 2",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_2_c,
                "temperature_3_C", "Probe 3",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_3_c,
                "temperature_4_C", "Probe 4",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_4_c,
                "temperature_5_C", "Probe 5",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_5_c,
                "ambient_C",    "Ambient",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, ambient_c,
                "mic",          "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return DECODE_FAIL_MIC;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "in_base",
        "counter",
        "battery_V",
        "temperature_1_C",
        "temperature_2_C",
        "temperature_3_C",
        "temperature_4_C",
        "temperature_5_C",
        "ambient_C",
        "mic",
        NULL,
};

r_device const typhur_sync_gold = {
        .name        = "Typhur Sync Gold meat thermometer probe",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 13,
        .long_width  = 13,
        .reset_limit = 3000,
        .decode_fn   = &typhur_sync_gold_decode,
        .fields      = output_fields,
};

r_device const thermomaven_wtp = {
        .name        = "ThermoMaven WTP Meat Thermometer Probe",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 13,
        .long_width  = 13,
        .reset_limit = 3000,
        .decode_fn   = &thermomaven_wtp_decode,
        .fields      = output_fields,
};
