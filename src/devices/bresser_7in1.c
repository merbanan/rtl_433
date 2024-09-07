/** @file
    Decoder for Bresser Weather Center 7-in-1 and Air quality sensors.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define SENSOR_TYPE_WEATHER   1
#define SENSOR_TYPE_AIR_PM    8
#define SENSOR_TYPE_CO2      10
#define SENSOR_TYPE_HCHO_VOC 11

/**
Decoder for Bresser Weather Center 7-in-1 and Air quality sensors.
- Air Quality PM2.5/PM10 PN 7009970
- CO2 sensor             PN 7009977
- HCHO/VOC sensor        PN 7009978

See
https://github.com/merbanan/rtl_433/issues/1492
and
https://github.com/merbanan/rtl_433/issues/2693

Preamble:

    aa aa aa aa aa 2d d4

Observed length depends on reset_limit.
The data (not including STYPE, STARTUP, CH and maybe ID) has a whitening of 0xaa.

Weather Center
Data layout:

    {271}631d05c09e9a18abaabaaaaaaaaa8adacbacff9cafcaaaaaaa000000000000000000


    {262}10b8b4a5a3ca10aaaaaaaaaaaaaa8bcacbaaaa2aaaaaaaaaaa0000000000000000 [0.08 klx]
    {220}543bb4a5a3ca10aaaaaaaaaaaaaa8bcacbaaaa28aaaaaaaaaa00000 [0.08 klx]
    {273}2492b4a5a3ca10aaaaaaaaaaaaaa8bdacbaaaa2daaaaaaaaaa0000000000000000000 [0.08klx]

    {269}9a59b4a5a3da10aaaaaaaaaaaaaa8bdac8afea28a8caaaaaaa000000000000000000 [54.0 klx UV=2.6]
    {230}fe15b4a5a3da10aaaaaaaaaaaaaa8bdacbba382aacdaaaaaaa00000000 [109.2klx   UV=6.7]
    {254}2544b4a5a32a10aaaaaaaaaaaaaa8bdac88aaaaabeaaaaaaaa00000000000000 [200.000 klx UV=14

    DIGEST:8h8h ID?8h8h WDIR:8h4h 4h 8h WGUST:8h.4h WAVG:8h.4h RAIN:8h8h4h.4h RAIN?:8h TEMP:8h.4hC FLAGS?:4h HUM:8h% LIGHT:8h4h,8h4hKL UV:8h.4h TRAILER:8h8h8h4h


Unit of light is kLux (not W/m²).

Air Quality Sensor PM2.5 / PM10 Sensor (PN 7009970)
Data layout:

    DIGEST:8h8h ID?8h8h ?8h8h STYPE:4h STARTUP:1b CH:3b ?8h 4h ?4h8h4h PM_2_5:4h8h4h PM10:4h8h4h ?4h ?8h4h BATT:1b ?3b ?8h8h8h8h8h8h TRAILER:8h8h8h

Air Quality Sensor CO2 (PN 7009977) : issue #2813

From user manual , co2 ppm is from 400 to 5000 ppm, so it's 16 bits coded.

Samples :
Raw :
                  SType Startup & Channel

                      | |
    {207}dab6d782acd9 a 1 ad9aad9aad9aaaaaaaaaaaaaaaaae99aaaaa00 Type = 0xa = 10, Startup = 0, ch = 1
    {207}04a9d782acd8 a 1 ad9aad9aad9aaaaaaaaaaaaaaaaae99aaaaa00 Type = 0xa = 10, Startup = 0, ch = 1
    {207}04a9d782acd8 a 1 ad9aad9aad9aaaaaaaaaaaaaaaaae99aaaaa00 Type = 0xa = 10, Startup = 0, ch = 1
    {207}0dd1d782b8ee a 1 ad9aad9aad9aaaaaaaaaaaaaaaaae99aaaaa00 Type = 0xa = 10, Startup = 0, ch = 1

Data layout raw :
    DIGEST:16h ID:16h 8x8x STYPE:4h STARTUP:1b CH:3d 8x8x8x8x8x8x8x8x8x8x8x8x8x8x8x8x8x8x TRAILER:8x

XOR / de-whitened :

          0 1  2 3  4 5  6 7 8 9101112131415161718192021222324
       DIGEST   ID  ppm                  bat
            |    |    |                    |
    {200}701c 7d28 0673 0b073007300730000000000000000043300000 [ XOR from g001_868.34M_1000k.cu8 co2 ppm  673]
    {200}ae03 7d28 0672 0b073007300730000000000000000043300000 [ XOR from g001_868.34M_250k.cu8  co2 ppm  672]
    {200}ae03 7d28 0672 0b073007300730000000000000000043300000 [ XOR from g002_868.34M_1000k.cu8 co2 ppm  672]
    {200}a77b 7d28 1244 0b073007300730000000000000000043300000 [ XOR from g002_868.34M_250k.cu8  co2 ppm 1244]

Data layout de-whitened :
    DIGEST:16h ID:16h PPM:16h 8x8x8x8x8x8x8x8x8x8x4x BATT:1b 3x8x8x8x8x8x8x TRAILER:16x

Air Quality Sensor HCHO/VOC (PN 7009978) : issue #2814

From user manual , hcho ppb is from 0 to 1000 ppm, so it's 16 bits coded.
              and voc level is from 1 (bad air quality) to 5 (good air quality), so it's 4 bits coded.

Samples:
Raw :
                  SType Startup & Channel
                      | |
    {207}3f2dc4a5aaaf b 1 aaa8aaa8aaa8aaaaaaaaaaaaaaaae9feaaaa00 Type = 0xb = 11, Startup = 0, ch = 1
    {207}0c1cc4a5aaaf b 1 aaa8aaa8aaa8aaaaaaaaaaaaaaaae9ffaaaa00 Type = 0xb = 11, Startup = 0, ch = 1
    {207}3f2dc4a5aaaf b 1 aaa8aaa8aaa8aaaaaaaaaaaaaaaae9feaaaa00 Type = 0xb = 11, Startup = 0, ch = 1
    {207}0c1cc4a5aaaf b 1 aaa8aaa8aaa8aaaaaaaaaaaaaaaae9ffaaaa00 Type = 0xb = 11, Startup = 0, ch = 1
    {207}61afc4a5aaa2 b 9 aaa8aaa8aaa9aaaaaaaaaaaaaaaae9f8aaaa00 Type = 0xb = 11, Startup = 1, ch = 1
    {207}ecddc4a5aaae b 9 aaa8aaa8aaa9aaaaaaaaaaaaaaaae9fbaaaa00 Type = 0xb = 11, Startup = 1, ch = 1

Data layout raw :
    DIGEST:16h ID:16h 8x8x STYPE:4h STARTUP:1b CH:3d 8x8x8x8x8x8x8x8x8x8x8x8x8x8x8x8x8x8x TRAILER:8x

XOR / de-whitened :

          0 1  2 3  4 5  6 7 8 9101112131415161718192021 22 2324
       DIGEST   ID  ppb                  bat            voc
            |    |    |                    |              |
    {200}9587 6e0f 0005 1b0002000200020000000000000000435 4 0000 [XOR from g001_868.34M_1000k.cu8 hcho_ppb 5 voc_level 4]
    {200}a6b6 6e0f 0005 1b0002000200020000000000000000435 5 0000 [XOR from g001_868.34M_250k.cu8  hcho_ppb 5 voc_level 5]
    {200}9587 6e0f 0005 1b0002000200020000000000000000435 4 0000 [XOR from g002_868.34M_1000k.cu8 hcho_ppb 5 voc_level 4]
    {200}a6b6 6e0f 0005 1b0002000200020000000000000000435 5 0000 [XOR from g001_868.34M_250k.cu8  hcho_ppb 5 voc_level 5]
    {200}cb05 6e0f 0008 130002000200030000000000000000435 2 0000 [XOR from g003_868.34M_1000k.cu8 hcho_ppb 8 voc_level 2]
    {200}4677 6e0f 0004 130002000200030000000000000000435 1 0000 [XOR from g004_868.34M_1000k.cu8 hcho_ppb 4 voc_level 1]

Data layout de-whitened :
    DIGEST:16h ID:16h PPB:16h 8x8x8x8x8x8x8x8x8x8x4x BATT:1b 3x8x8x8x8x8x4x VOC:4h TRAILER:16x

#2816 Bresser Air Quality sensors, ignore first packet:
    The first signal is not sending the good BCD values , all at 0xF and need to be excluded from result (BCD value can't be > 9) .

First two bytes are an LFSR-16 digest, generator 0x8810 key 0xba95 with a final xor 0x6df1, which likely means we got that wrong.
*/

static int bresser_7in1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0xaa, 0x2d, 0xd4};

    data_t *data;
    uint8_t msg[25];

    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] < 240 - 80) {
        decoder_logf(decoder, 2, __func__, "to few bits (%u)", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH; // unrecognized
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);
    start_pos += sizeof(preamble_pattern) * 8;

    if (start_pos >= bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 2, __func__, "preamble not found");
        return DECODE_ABORT_EARLY; // no preamble found
    }
    //if (start_pos + sizeof (msg) * 8 >= bitbuffer->bits_per_row[0]) {
    if (start_pos + 21*8 >= bitbuffer->bits_per_row[0]) {
        decoder_logf(decoder, 2, __func__, "message too short (%u)", bitbuffer->bits_per_row[0] - start_pos);
        return DECODE_ABORT_LENGTH; // message too short
    }

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, sizeof (msg) * 8);
    decoder_log_bitrow(decoder, 2, __func__, msg, sizeof(msg) * 8, "MSG");

    if (msg[21] == 0x00) {
        return DECODE_FAIL_SANITY;
    }

    int s_type   = msg[6] >> 4;
    int nstartup = (msg[6] & 0x08) >> 3;
    int chan     = msg[6] & 0x07;

    // data whitening
    for (unsigned i = 0; i < sizeof (msg); ++i) {
        msg[i] ^= 0xaa;
    }
    decoder_log_bitrow(decoder, 2, __func__, msg, sizeof(msg) * 8, "XOR");

    // LFSR-16 digest, generator 0x8810 key 0xba95 final xor 0x6df1
    int chk    = (msg[0] << 8) | msg[1];
    int digest = lfsr_digest16(&msg[2], 23, 0x8810, 0xba95);
    if ((chk ^ digest) != 0x6df1) {
        decoder_logf(decoder, 2, __func__, "Digest check failed %04x vs %04x (%04x)", chk, digest, chk ^ digest);
        return DECODE_FAIL_MIC;
    }

    int id          = (msg[2] << 8) | (msg[3]);
    int flags       = (msg[15] & 0x0f);
    int battery_low = (flags & 0x06) == 0x06;

    if (s_type == SENSOR_TYPE_WEATHER) {
        int wdir     = (msg[4] >> 4) * 100 + (msg[4] & 0x0f) * 10 + (msg[5] >> 4);
        int wgst_raw = (msg[7] >> 4) * 100 + (msg[7] & 0x0f) * 10 + (msg[8] >> 4);
        int wavg_raw = (msg[8] & 0x0f) * 100 + (msg[9] >> 4) * 10 + (msg[9] & 0x0f);
        int rain_raw = (msg[10] >> 4) * 100000 + (msg[10] & 0x0f) * 10000 + (msg[11] >> 4) * 1000
                + (msg[11] & 0x0f) * 100 + (msg[12] >> 4) * 10 + (msg[12] & 0x0f) * 1; // 6 digits
        float rain_mm = rain_raw * 0.1f;
        int temp_raw = (msg[14] >> 4) * 100 + (msg[14] & 0x0f) * 10 + (msg[15] >> 4);
        float temp_c = temp_raw * 0.1f;

        if (temp_raw > 600)
            temp_c = (temp_raw - 1000) * 0.1f;
        int humidity = (msg[16] >> 4) * 10 + (msg[16] & 0x0f);
        int lght_raw = (msg[17] >> 4) * 100000 + (msg[17] & 0x0f) * 10000 + (msg[18] >> 4) * 1000
                + (msg[18] & 0x0f) * 100 + (msg[19] >> 4) * 10 + (msg[19] & 0x0f);
        int uv_raw =   (msg[20] >> 4) * 100 + (msg[20] & 0x0f) * 10 + (msg[21] >> 4);

        float light_klx = lght_raw * 0.001f; // TODO: remove this
        float light_lux = lght_raw;
        float uv_index = uv_raw * 0.1f;

        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Bresser-7in1",
                "id",               "",             DATA_INT,    id,
                "startup",          "Startup",      DATA_COND,   !nstartup,  DATA_INT, !nstartup,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                "humidity",         "Humidity",     DATA_INT,    humidity,
                "wind_max_m_s",     "Wind Gust",    DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wgst_raw * 0.1f,
                "wind_avg_m_s",     "Wind Speed",   DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wavg_raw * 0.1f,
                "wind_dir_deg",     "Direction",    DATA_INT,    wdir,
                "rain_mm",          "Rain",         DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_mm,
                "light_klx",        "Light",        DATA_FORMAT, "%.3f klx", DATA_DOUBLE, light_klx, // TODO: remove this
                "light_lux",        "Light",        DATA_FORMAT, "%.3f lux", DATA_DOUBLE, light_lux,
                "uv",               "UV Index",     DATA_FORMAT, "%.1f", DATA_DOUBLE, uv_index,
                "battery_ok",       "Battery",      DATA_INT,    !battery_low,
                "mic",              "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;

    } else if (s_type == SENSOR_TYPE_AIR_PM) {
        int pm_2_5      = (msg[10] & 0x0f) * 1000 + (msg[11] >> 4) * 100 + (msg[11] & 0x0f) * 10 + (msg[12] >> 4);
        int pm_10       = (msg[12] & 0x0f) * 1000 + (msg[13] >> 4) * 100 + (msg[13] & 0x0f) * 10 + (msg[14] >> 4);
        int pm_2_5_init = (msg[10] & 0x0f) == 0x0f; // confirmed by https://github.com/merbanan/rtl_433/issues/2816#issuecomment-1935439318
        int pm_10_init  = (msg[12] & 0x0f) == 0x0f; // confirmed by https://github.com/merbanan/rtl_433/issues/2816#issuecomment-1935439318

        /* clang-format off */
        data = data_make(
                "model",            "",                         DATA_STRING, "Bresser-7in1",  // should be Bresser-Air-PM
                "id",               "",                         DATA_INT,    id,
                "channel",          "",                         DATA_INT,    chan,
                "startup",          "Startup",                  DATA_COND,   !nstartup,   DATA_INT, !nstartup,
                "battery_ok",       "Battery",                  DATA_INT,    !battery_low,
                "pm2_5_ug_m3",      "PM2.5 Mass Concentration", DATA_COND,   !pm_2_5_init,   DATA_INT, pm_2_5,
                "pm10_0_ug_m3",     "PM10 Mass Concentraton",   DATA_COND,   !pm_10_init,    DATA_INT, pm_10,
                "mic",              "Integrity",                DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;

    } else if (s_type == SENSOR_TYPE_CO2) {
        int co2      = ((msg[4]& 0xf0) >> 4) * 1000 + (msg[4]& 0x0f) * 100 + ((msg[5]& 0xf0) >> 4) * 10 + (msg[5] & 0x0f);
        int co2_init = (msg[5] & 0x0f) == 0x0f;

        /* clang-format off */
        data = data_make(
                "model",            "",                         DATA_STRING, "Bresser-CO2",
                "id",               "",                         DATA_INT,    id,
                "channel",          "",                         DATA_INT,    chan,
                "startup",          "Startup",                  DATA_COND,   !nstartup,  DATA_INT, !nstartup,
                "battery_ok",       "Battery",                  DATA_INT,    !battery_low,
                "co2_ppm",          "Carbon Dioxide",           DATA_COND,   !co2_init,     DATA_FORMAT, "%d ppm", DATA_INT, co2,
                "mic",              "Integrity",                DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;

    } else if (s_type == SENSOR_TYPE_HCHO_VOC) {
        int hcho      = ((msg[4]& 0xf0) >> 4) * 1000 + (msg[4]& 0x0f) * 100 + ((msg[5]& 0xf0) >> 4) * 10 + (msg[5] & 0x0f);
        int voc       = (msg[22]& 0x0f);
        int hcho_init = (msg[5] & 0x0f) == 0x0f;
        int voc_init  = voc == 0x0f;

        /* clang-format off */
        data = data_make(
                "model",            "",                           DATA_STRING, "Bresser-HCHOVOC",
                "id",               "",                           DATA_INT,    id,
                "channel",          "",                           DATA_INT,    chan,
                "startup",          "Startup",                    DATA_COND,   !nstartup,  DATA_INT, !nstartup,
                "battery_ok",       "Battery",                    DATA_INT,    !battery_low,
                "hcho_ppb",         "Formaldehyde",               DATA_COND,   !hcho_init, DATA_FORMAT, "%d ppb", DATA_INT, hcho,
                "voc_level",        "Volatile Organic Compounds", DATA_COND,   !voc_init,  DATA_FORMAT, "%d",     DATA_INT, voc, // from 1 bad air quality to 5 very good air quality
                "mic",              "Integrity",                  DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;

        // To Do: identify further data

    } else {
        decoder_logf(decoder, 2, __func__, "DECODE_FAIL_SANITY, s_type=%d not implemented", s_type);
        return DECODE_FAIL_SANITY;

    }
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "startup",
        "temperature_C",
        "humidity",
        "wind_max_m_s",
        "wind_avg_m_s",
        "wind_dir_deg",
        "rain_mm",
        "light_klx", // TODO: remove this
        "light_lux",
        "uv",
        "pm2_5_ug_m3",
        "pm10_0_ug_m3",
        "battery_ok",
        "co2_ppm",
        "hcho_ppb",
        "voc_level",
        "mic",
        NULL,
};

r_device const bresser_7in1 = {
        .name        = "Bresser Weather Center 7-in-1, Air Quality PM2.5/PM10 7009970, CO2 7009977, HCHO/VOC 7009978 sensors",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 124,
        .long_width  = 124,
        .reset_limit = 25000,
        .decode_fn   = &bresser_7in1_decode,
        .fields      = output_fields,
};
