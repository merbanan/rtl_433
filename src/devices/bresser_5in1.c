/** @file
    Decoder for Bresser Weather Center 5-in-1.

    Copyright (C) 2018 Daniel Krueger
    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decoder for Bresser Weather Center 5-in-1.

The compact 5-in-1 multifunction outdoor sensor transmits the data on 868.3 MHz.
The device uses FSK-PCM encoding,
The device sends a transmission every 12 seconds.
A transmission starts with a preamble of 0xAA.

Decoding borrowed from https://github.com/andreafabrizi/BresserWeatherCenter

Preamble:

    aa aa aa aa aa 2d d4

Packet payload without preamble (203 bits):

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
    -----------------------------------------------------------------------------
    ed ee 46 ff ff ff ef 9f ff 8b 7d eb ff 12 11 b9 00 00 00 10 60 00 74 82 14 00 00 00 (Rain Gauge)
    e9 ee 46 ff ff ff ef 99 ff 8b 8b eb ff 16 11 b9 00 00 00 10 66 00 74 74 14 00 00 00 (Rain Gauge)
    ee 93 7f f7 bf fb ef 9e fe ae bf ff ff 11 6c 80 08 40 04 10 61 01 51 40 00 00
    ed 93 7f ff 0f ff ef b8 fe 7d bf ff ff 12 6c 80 00 f0 00 10 47 01 82 40 00 00
    eb 93 7f eb 9f ee ef fc fc d6 bf ff ff 14 6c 80 14 60 11 10 03 03 29 40 00 00
    ed 93 7f f7 cf f7 ef ed fc ce bf ff ff 12 6c 80 08 30 08 10 12 03 31 40 00 00
    f1 fd 7f ff af ff ef bd fd b7 c9 ff ff 0e 02 80 00 50 00 10 42 02 48 36 00 00 00 00 (from https://github.com/merbanan/rtl_433/issues/719#issuecomment-388896758)
    ee b7 7f ff 1f ff ef cb fe 7b d7 fc ff 11 48 80 00 e0 00 10 34 01 84 28 03 00 (from https://github.com/andreafabrizi/BresserWeatherCenter)
    e3 fd 7f 89 7e 8a ed 68 fe af 9b fd ff 1c 02 80 76 81 75 12 97 01 50 64 02 00 00 00 (Large Wind Values, Gust=37.4m/s Avg=27.5m/s from https://github.com/merbanan/rtl_433/issues/1315)
    ef a1 ff ff 1f ff ef dc ff de df ff 7f 10 5e 00 00 e0 00 10 23 00 21 20 00 80 00 00 (low batt +ve temp)
    ed a1 ff ff 1f ff ef 8f ff d6 df ff 77 12 5e 00 00 e0 00 10 70 00 29 20 00 88 00 00 (low batt -ve temp -7.0C)
    ec 91 ff ff 1f fb ef e7 fe ad ed ff f7 13 6e 00 00 e0 04 10 18 01 52 12 00 08 00 00 (good batt -ve temp)
    CC CC CC CC CC CC CC CC CC CC CC CC CC uu II sS GG DG WW  W TT  T HH RR RR Bt
                                              G-MSB ^     ^ W-MSB  (strange but consistent order)

- C = Check, inverted data of 13 byte further
- uu = checksum (number/count of set bits within bytes 14-25)
- I = station ID (maybe)
- G = wind gust in 1/10 m/s, normal binary coded, GGxG = 0x76D1 => 0x0176 = 256 + 118 = 374 => 37.4 m/s.  MSB is out of sequence.
- D = wind direction 0..F = N..NNE..E..S..W..NNW
- W = wind speed in 1/10 m/s, BCD coded, WWxW = 0x7512 => 0x0275 = 275 => 27.5 m/s. MSB is out of sequence.
- T = temperature in 1/10 °C, BCD coded, TTxT = 1203 => 31.2 °C
- t = temperature sign, minus if unequal 0
- H = humidity in percent, BCD coded, HH = 23 => 23 %
- R = rain in mm, BCD coded, RRRR = 1203 => 031.2 mm
- B = Battery. 0=Ok, 8=Low.
- s = startup, 0 after power-on/reset / 8 after 1 hour
- S = sensor type, only low nibble used, 0x9 for Bresser Professional Rain Gauge
*/

static int bresser_5in1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0xaa, 0x2d, 0xd4};

    data_t *data;
    uint8_t msg[26];
    uint16_t sensor_id;
    unsigned len = 0;

    if (bitbuffer->num_rows != 1
            || bitbuffer->bits_per_row[0] < 248
            || bitbuffer->bits_per_row[0] > 440) {
        decoder_logf(decoder, 2, __func__, "bit_per_row %u out of range", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_EARLY; // Unrecognized data
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof (preamble_pattern) * 8);

    if (start_pos == bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }
    start_pos += sizeof (preamble_pattern) * 8;
    len = bitbuffer->bits_per_row[0] - start_pos;
    if (((len + 7) / 8) < sizeof (msg)) {
        decoder_logf(decoder, 2, __func__, "%u too short", len);
        return DECODE_ABORT_LENGTH; // message too short
    }
    // truncate any excessive bits
    len = MIN(len, sizeof (msg) * 8);

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, len);

    // First 13 bytes need to match inverse of last 13 bytes
    for (unsigned col = 0; col < sizeof (msg) / 2; ++col) {
        if ((msg[col] ^ msg[col + 13]) != 0xff) {
            decoder_logf(decoder, 2, __func__, "Parity wrong at %u", col);
            return DECODE_FAIL_MIC; // message isn't correct
        }
    }

    // check popcount
    // uu = checksum (number/count of set bits within bytes 14-25)

    sensor_id = msg[14];

    int temp_raw = (msg[20] & 0x0f) + ((msg[20] & 0xf0) >> 4) * 10 + (msg[21] &0x0f) * 100;
    if (msg[25] & 0x0f)
        temp_raw = -temp_raw;
    float temperature = temp_raw * 0.1f;

    int humidity = (msg[22] & 0x0f) + ((msg[22] & 0xf0) >> 4) * 10;

    float wind_direction_deg = ((msg[17] & 0xf0) >> 4) * 22.5f;

    int gust_raw = ((msg[17] & 0x0f) << 8) + msg[16]; //fix merbanan/rtl_433#1315
    float wind_gust = gust_raw * 0.1f;

    int wind_raw = (msg[18] & 0x0f) + ((msg[18] & 0xf0) >> 4) * 10 + (msg[19] & 0x0f) * 100; //fix merbanan/rtl_433#1315
    float wind_avg = wind_raw * 0.1f;

    int rain_raw = (msg[23] & 0x0f) + ((msg[23] & 0xf0) >> 4) * 10 + (msg[24] & 0x0f) * 100 + ((msg[24] & 0xf0) >> 4) * 1000;
    float rain = rain_raw * 0.1f;

    int battery_low = (msg[25] & 0x80);

    /* check if the message is from a Bresser Professional Rain Gauge */
    if ((msg[15] & 0xF) == 0x9) {
        // rescale the rain sensor readings
        rain = rain * 2.5;
        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Bresser-ProRainGauge",
                "id",               "",             DATA_INT,    sensor_id,
                "battery_ok",       "Battery",      DATA_INT,    !battery_low,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C",      DATA_DOUBLE, temperature,
                "rain_mm",          "Rain",         DATA_FORMAT, "%.1f mm",     DATA_DOUBLE, rain,
                "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */
    } else {

        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Bresser-5in1",
                "id",               "",             DATA_INT,    sensor_id,
                "battery_ok",       "Battery",      DATA_INT,    !battery_low,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C",      DATA_DOUBLE, temperature,
                "humidity",         "Humidity",     DATA_INT,    humidity,
                "wind_max_m_s",     "Wind Gust",    DATA_FORMAT, "%.1f m/s",    DATA_DOUBLE, wind_gust,
                "wind_avg_m_s",     "Wind Speed",   DATA_FORMAT, "%.1f m/s",    DATA_DOUBLE, wind_avg,
                "wind_dir_deg",     "Direction",    DATA_FORMAT, "%.1f",        DATA_DOUBLE, wind_direction_deg,
                "rain_mm",          "Rain",         DATA_FORMAT, "%.1f mm",     DATA_DOUBLE, rain,
                "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */
    }
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "humidity",
        "wind_max_m_s",
        "wind_avg_m_s",
        "wind_dir_deg",
        "rain_mm",
        "mic",
        NULL,
};

r_device const bresser_5in1 = {
        .name        = "Bresser Weather Center 5-in-1",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 124,
        .long_width  = 124,
        .reset_limit = 25000,
        .decode_fn   = &bresser_5in1_decode,
        .fields      = output_fields,
};
