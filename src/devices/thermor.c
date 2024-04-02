/** @file
    Thermor DG950 weather station.

    Copyright (C) 2024 Nicolas Gagné, Bruno OCTAU (ProfBoc75)
    Copyright (C) 2024 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Thermor DG950 weather station.

The weather station is composed of:
- Display Receiver DG950R, FCC test reports are available at: https://fccid.io/S24DG950R
- Thermometer-Transmitter Sensor DG950, FCC test reports are available at: https://fccid.io/S24DG950
- Wind Sensor (speed and direction) DG950
- Rain Gauge Sensor (cumulative rainfall) DG950

The manual is available at: https://fccid.io/S24DG950R/Users-Manual/USERS-MANUAL-522434
Review of the station: https://www.home-weather-stations-guide.com/thermor-weather-station.html


S.a #2879 open by Nicolas Gagné.

RF raw Signal: 96 synchro pulses, 13 x [gap, start 1 bit 0, 8 bit]

    {213}0000000000000000000000000c3adfe6b1f0f92eff258f4fe1f0f8

Flex decoder:

    rtl_433 -X 'n=thermor,m=OOK_PWM,s=750,l=2128,y=1438,g=3000,r=8000,get=byte:@1:{8}:%x'

    {9}0c0, {9}758, {9}7f8, {9}358, {9}1f0, {9}1f0, {9}4b8, {9}7f8, {9}258, {9}1e8, {9}3f8, {9}0f8, {9}0f8

Samples here from Nicolas Gagné:

https://github.com/NicolasGagne/rtl_433_tests/tree/a20b49805ed7ba74db016ec43e2a34ccda8231a9/tests/Thermor%20DG950

Data Layout: (normal mode)

    Byte position             0          1          2          3           4           5          6          7          8          9         10         11         12
    bit position     0 12345678 0 12345678 0 12345678 0 12345678 0 1234 5678 0 1234 5678 0 12345678 0 12345678 0 12345678 0 12345678 0 12345678 0 12345678 0 12345678
    Data             X IIIIIIII X TEMP_INT X Rain_mm  X TEMP_CHK X WDIR FLAG X WDIR FLAG X AAAA_LSB X AAAA_MSB X BBBBBBBB X CCCC_CHK X TEMP_DEC X ???????? X Rain_mm+7

All bytes are reflected/reverse8

- II:{8}        ID
- TEMP_INT:{8}  temperature interger part, offset +195, °C
- TEMP_DEC:{8}  temperature decimal part, offset +245, scale 10
- RTmm:{4}      rain rate in 0.1 mm
- RCHK:{4}      rain rate in 0.1 mm offset +7 , used to check if same as RTmm
- TEMP_CHK:{8}  Temp checksum
- WDIR:{4}      wind direction, map table to be used
- FLAG:{4}      if 1  = valid win dir, if 0 wind dir = unknown
- A_L,  A_M,  B,  C_CHK :{8} values related to Wind Speed, C = A_L + A_M + B
- ?:{8} or {4}  unknown values, fixed.
- X:{1}         Bit start 0 is ignore


*/

static int thermor_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 13) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t b[13];

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] != 9) {
            return DECODE_ABORT_EARLY;
        }
        // test is start bit = 0
        if ((bitbuffer->bb[row][0] & 0x80) != 0) {
            return DECODE_ABORT_EARLY;
        }
        // extract only the 8 bit, ignore the start bit 0
        bitbuffer_extract_bytes(bitbuffer, row, 1, &b[row], 8);
    }
    // Test if Sync/pairing or normal signal
    reflect_bytes(b,13);
    decoder_log_bitrow(decoder, 1, __func__, b, sizeof(b) * 8, "Reflected");

    int const wind_dir_degr[] = {157,45,135,67,180,22,112,90,225,337,247,315,202,0,270,292};

    //sync pairing mode
    if (b[0] == 0xff && b[1] == b[2] && b[1] == b[4] && b[1] == b[5] && b[1] == b[6] && b[1] == b[7] && b[1] == b[8] && b[1] == b[10]  ) {
        int new_id = ~b[1] & 0xff;
        /* clang-format off */
        data_t *data = data_make(
                "model",          "",           DATA_STRING, "Thermor-DG950",
                "id",         "",               DATA_FORMAT, "%d", DATA_INT, new_id,
                "pairing",         "Pairing?",  DATA_INT, 1,
                "mic",            "Integrity",  DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }

    else {
        decoder_logf(decoder, 2, __func__, "Start decode ...");
        float wind_ratio = 0;
        int have_rain = 0;
        int have_wdir = 0;
        int have_wspd = 0;
        int rain_rate1 = 0;
        int rain_rate2 = 0;
        int wind_dir_d = 0;
        int wind_coef  = 0;
        float wind_speed_kmh = 0;

        int id        = ~b[0] & 0xff;
        decoder_logf(decoder, 1, __func__, "ID %d", id);

        // Temp Checksum
        int temp_chk = (b[1] + b[10]) & 0xff;
        if ( (temp_chk + 1) != (b[3] & 0xff) ) {
            decoder_logf(decoder, 2, __func__, "Temp Check Sum failed %d %d", temp_chk, (b[3] & 0xff));
            return DECODE_ABORT_EARLY;
        }

        float temp_int   = b[1] - 195;
        float temp_dec   = (b[10] - 245) * 0.1f;
        float temp_C     = temp_int + temp_dec;
        decoder_logf(decoder, 2, __func__, "Temp %f", temp_C);

        // Test if valid rain
        rain_rate1 = (~b[2] & 0xff);
        rain_rate2 = (~b[12] & 0xff) -7;
        // Rain check
        if (rain_rate1 != rain_rate2) {
            decoder_log(decoder, 2, __func__, "Rain Check failed");
            return DECODE_ABORT_EARLY;
        }
        have_rain = 1;
        decoder_logf(decoder, 1, __func__, "Rain check passed ...");

        // Test if valid wind direction
        if ( b[4] != 0xff && b[5] != 0xff) {
            if (b[4] != b[5]) {
                decoder_log(decoder, 2, __func__, "Wind Direction Check failed");
                return DECODE_ABORT_EARLY;
            }
            wind_dir_d  = wind_dir_degr[(b[4] & 0x0f)];
            have_wdir = 1;
        }

        // Wind speed check sum
        int wind_chk = (~b[6] + ~b[7] + ~b[8]) & 0xff;
        if (wind_chk != (~b[9] & 0xff)) {
            decoder_logf(decoder, 2, __func__, "Wind Check Sum failed %d %d", wind_chk, (~b[9] & 0xff));
            return DECODE_ABORT_EARLY;
        }
        decoder_logf(decoder, 2, __func__, "Wind Speed check passed ...");
        if (b[8] != 0xff ) {
            uint16_t wind_speed_raw = (~b[6] & 0xff) | ((~b[7] & 0xff) << 8) ;
            wind_coef  = ~b[8] & 0xff;
            if (wind_speed_raw < 256) {
                wind_ratio = (wind_speed_raw * -0.0001746) + 0.155;
            }
            else {
                wind_ratio = 0.11;
            }
            wind_speed_kmh = wind_ratio * (wind_speed_raw - wind_coef + 45);
            if (wind_speed_kmh < 0 ) {
                wind_speed_kmh = 0;
            }
            have_wspd = 1;
            decoder_logf(decoder, 2, __func__, "Wind Speed calc passed ...");
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",          "",               DATA_STRING, "Thermor-DG950",
                "id",             "",               DATA_FORMAT, "%d",    DATA_INT,    id,
                "temperature_C",  "Temperature",    DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_C,
                "rain_rate_mm_h", "Rain Rate",      DATA_COND, have_rain, DATA_FORMAT, "%.1f mm/h", DATA_DOUBLE, rain_rate1 * 0.1f,
                "wind_dir_deg",   "Wind Direction", DATA_COND, have_wdir, DATA_INT,     wind_dir_d,
                "wind_avg_km_h",  "Wind avg speed", DATA_COND, have_wspd, DATA_FORMAT, "%.1f km/h", DATA_DOUBLE, wind_speed_kmh,
                //"wind_coef",      "Wind Coef",      DATA_COND, have_wspd, DATA_INT,     wind_coef,
                //"wind_ratio",     "Wind Ratio",     DATA_COND, have_wspd, DATA_DOUBLE,  wind_ratio,
                "pairing",         "Pairing?",      DATA_INT, 0,
                "mic",            "Integrity",      DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    return 0;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "wind_avg_km_h",
        "rain_rate_mm_h",
        "wind_dir_deg",
        "wind_ratio",
        "wind_coef",
        "pairing",
        "mic",
        NULL,
};

r_device const thermor = {
        .name        = "Thermor DG950 weather station",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 680,
        .long_width  = 2100,
        .sync_width  = 1438,
        .gap_limit   = 3000,
        .reset_limit = 8000,
        .decode_fn   = &thermor_decode,
        .fields      = output_fields,
};
