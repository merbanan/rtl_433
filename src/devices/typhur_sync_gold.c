/** @file
    Typhur Sync Gold meat thermometer probe (Dual/Quad variants).

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"
#include <math.h>

/**
Typhur Sync Gold / Thermomaven WT10 meat thermometer probe.

Reverse engineered in issue #3138 by \@hakong and \@kevin-david, with
guidance from \@zuckschwerdt. FSK_PCM at 12.5 us/bit, long 0xaa
preamble, 16 bit sync word `0x5754`, 24 byte payload:

    ID:24h ?:8h STATUS:8h VARIANT:8h T1:16h T2:16h T3:16h T4:16h T5:16h
    AMBIENT:16h BATTERY:16h COUNTER:16h CRC:16h

- ID: 24 bit, one per physical probe
- STATUS: bit 3 set when the probe is seated in its charging base
- VARIANT (byte 5): model/firmware tag, constant per model. 0x1d =
  Thermomaven WT10, 0x57 = Typhur Sync Gold. See below.
- T1-T5: probe temperature sensors, little-endian
- AMBIENT: little-endian, separate thermistor
- BATTERY: little-endian; scale is per-model (Typhur 0.01 V, WT10
  0.01/3 V)
- COUNTER: little-endian, increments every transmission
- CRC: CRC-16 poly 0x8005 init 0x0000 over the preceding 22 bytes

Temperature encoding differs by model, selected by the byte-5 variant
tag (constant per model, distinct between models -- confirmed against
CRC-valid captures from two Typhur units and one WT10 unit).

Thermomaven WT10 (variant 0x1d): each 16-bit field is the raw ADC code
of an NTC thermistor read through a voltage divider, not a scaled
temperature (the code decreases as temperature rises). Recover the
resistance as R = code / (N - code) (the series resistor folds into
the constants) and apply Steinhart-Hart:

    1/T[K] = A + B*ln(R) + C*ln(R)^2 + D*ln(R)^3

The probe channels and the ambient sensor use different thermistors,
so each has its own constants, reverse engineered for unit id 913719
by capturing raw codes alongside the Thermomaven app. Probes were
fitted across an ice bath (~0 C), room (~25-28 C), mid-range points
(~48-57 C) and a rolling boil (~98 C): worst-case residual 0.25 C over
0-98 C (the ln(R)^2 term is unused, C == 0), monotonic but uncalibrated
extrapolation above ~98 C. Ambient additionally used a ~61 C and two
~125 C grill points and needs the full four-term fit: worst-case
residual 0.4 C over 0-125 C, extrapolation (tends to overshoot) above
~125 C.

Ambient/battery status flag: a per-frame flag rides in bit 0x0800 of
the battery field. When set it also forces bit 0x8000 of the ambient
field high, which otherwise rolls a high ambient reading over to a
bogus cold value. The decoder strips the flag from the battery field
and masks the spurious ambient bit when the flag is set (when clear,
bit 0x8000 is a legitimate cold-end magnitude bit).

Typhur Sync Gold (variant 0x57): uses a different thermistor whose raw
codes do NOT fit the WT10 curve, and no app ground truth was available.
Its fields are reported with the original issue-#3138 linear scale
(probes * 0.01 C, ambient * 0.1 C). NOTE: that scale is almost
certainly wrong -- the fields are raw NTC codes, same as the WT10 --
but it is retained until Typhur app ground truth allows a proper
Steinhart-Hart fit. Treat Typhur temperatures as unverified.
*/

#define TYPHUR_SYNC_GOLD_PAYLOAD_LEN 24

typedef enum {
    TYPHUR_CONV_STEINHART, // NTC divider + Steinhart-Hart (calibrated)
    TYPHUR_CONV_LINEAR,    // legacy raw * scale (unverified)
} typhur_conv_t;

typedef struct {
    uint8_t variant; // byte-5 tag
    char const *model;
    typhur_conv_t conv;
    // Voltage-divider full-scale + Steinhart-Hart coefficients for the
    // 1, ln(R), ln(R)^2 and ln(R)^3 terms (probe, then ambient). The probe
    // needs only three terms (c == 0); ambient needs the ln(R)^2 term to
    // span its wider 0-125 C range.
    float probe_n, probe_a, probe_b, probe_c, probe_d;
    float amb_n, amb_a, amb_b, amb_c, amb_d;
    // Linear scale (probe, ambient) for TYPHUR_CONV_LINEAR.
    float probe_scale, amb_scale;
    // Battery voltage scale (the raw field is scaled differently per model).
    float battery_scale;
} typhur_variant_t;

static typhur_variant_t const typhur_variants[] = {
        {0x1d, "Thermomaven-WT10", TYPHUR_CONV_STEINHART,
                39740.0f, 3.336648967e-03f, 2.907677078e-04f, 0.0f, -8.446529470e-07f,
                48420.0f, 3.273739948e-03f, 3.237830150e-04f, 5.655028386e-05f, 1.257351231e-05f,
                0.0f, 0.0f, 0.01f / 3.0f},
        {0x57, "Typhur-SyncGold", TYPHUR_CONV_LINEAR,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0.01f, 0.1f, 0.01f},
};

static uint8_t const typhur_sync_gold_sync[2] = {0x57, 0x54};

/// Convert a raw NTC ADC code to degrees Celsius via divider + Steinhart-Hart.
static float typhur_sync_gold_steinhart(int code, float n, float a, float b, float c, float d)
{
    // Guard the divider/log domain: code must stay within (0, N).
    if (code < 1) {
        code = 1;
    }
    if ((float)code > n - 1.0f) {
        code = (int)(n - 1.0f);
    }
    float lr  = logf((float)code / (n - (float)code));
    float t_k = 1.0f / (a + b * lr + c * lr * lr + d * lr * lr * lr);
    return t_k - 273.15f;
}

/// Convert a raw field to degrees Celsius using the variant's model.
static float typhur_sync_gold_temp_c(typhur_variant_t const *v, int code, int is_ambient)
{
    if (v->conv == TYPHUR_CONV_LINEAR) {
        return code * (is_ambient ? v->amb_scale : v->probe_scale);
    }
    if (is_ambient) {
        return typhur_sync_gold_steinhart(code, v->amb_n, v->amb_a, v->amb_b, v->amb_c, v->amb_d);
    }
    return typhur_sync_gold_steinhart(code, v->probe_n, v->probe_a, v->probe_b, v->probe_c, v->probe_d);
}

static int typhur_sync_gold_decode(r_device *decoder, bitbuffer_t *bitbuffer)
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

        uint8_t b[TYPHUR_SYNC_GOLD_PAYLOAD_LEN];
        bitbuffer_extract_bytes(bitbuffer, row, pos, b, TYPHUR_SYNC_GOLD_PAYLOAD_LEN * 8);

        uint16_t crc = crc16(b, 22, 0x8005, 0x0000);
        uint16_t crc_recv = (b[22] << 8) | b[23];
        if (crc != crc_recv) {
            decoder_logf(decoder, 1, __func__, "CRC invalid %04x != %04x", crc, crc_recv);
            continue;
        }

        // Identify the model from the byte-5 variant tag.
        typhur_variant_t const *v = NULL;
        for (unsigned i = 0; i < sizeof(typhur_variants) / sizeof(typhur_variants[0]); i++) {
            if (typhur_variants[i].variant == b[5]) {
                v = &typhur_variants[i];
                break;
            }
        }
        if (!v) {
            decoder_logf(decoder, 1, __func__, "unknown variant tag %02x", b[5]);
            continue;
        }

        uint32_t id = ((uint32_t)b[0] << 16) | (b[1] << 8) | b[2];
        int in_base = (b[4] & 0x08) != 0;

        // A per-frame status flag rides in bit 0x0800 of the battery field.
        // When set it also forces bit 0x8000 of the ambient field high,
        // which otherwise rolls the ambient reading over (e.g. 3052 -> 35820,
        // decoding a ~115 C grill temp as -2.5 C). Strip the flag from the
        // battery field, and mask the spurious ambient bit when it is set --
        // when clear, bit 0x8000 is a legitimate part of a cold-end code.
        int flag        = (b[19] & 0x08) != 0;
        int amb_code    = b[16] | (b[17] << 8);
        if (flag) {
            amb_code &= 0x7fff;
        }
        int batt_raw = (b[18] | (b[19] << 8)) & ~0x0800;

        float temp_1_c  = typhur_sync_gold_temp_c(v, b[6] | (b[7] << 8), 0);
        float temp_2_c  = typhur_sync_gold_temp_c(v, b[8] | (b[9] << 8), 0);
        float temp_3_c  = typhur_sync_gold_temp_c(v, b[10] | (b[11] << 8), 0);
        float temp_4_c  = typhur_sync_gold_temp_c(v, b[12] | (b[13] << 8), 0);
        float temp_5_c  = typhur_sync_gold_temp_c(v, b[14] | (b[15] << 8), 0);
        float ambient_c = typhur_sync_gold_temp_c(v, amb_code, 1);
        float battery_v = batt_raw * v->battery_scale;
        int counter     = b[20] | (b[21] << 8);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, v->model,
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
        .name        = "Typhur Sync Gold / Thermomaven WT10 meat thermometer probe",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 13,
        .long_width  = 13,
        .reset_limit = 3000,
        .decode_fn   = &typhur_sync_gold_decode,
        .fields      = output_fields,
};
