/** @file
    Decoder for Bresser Weather Center 6-in-1.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decoder for Bresser Weather Center 6-in-1.

- also Bresser Weather Center 7-in-1 indoor sensor.
- also Bresser new 5-in-1 sensors.
- also Froggit WH6000 sensors.
- also rebranded as Ventus C8488A (W835)
- also Bresser 3-in-1 Professional Wind Gauge / Anemometer, PN 7002531
- also Bresser soil temperature and moisture meter, PN 7009972
- also Bresser Thermo-/Hygro-Sensor 7 Channel 868 MHz, PN 7009999
- also Bresser Pool / Spa Thermometer, PN 7009973 (STYPE = 3)

There are at least two different message types:
- 24 seconds interval for temperature, hum, uv and rain (alternating messages)
- 12 seconds interval for wind data (every message)

Also Bresser Explore Scientific SM60020 Soil moisture Sensor.
https://www.bresser.de/en/Weather-Time/Accessories/EXPLORE-SCIENTIFIC-Soil-Moisture-and-Soil-Temperature-Sensor.html

Moisture:

    f16e 187000e34 7 ffffff0000 252 2 16 fff 004 000 [25,2, 99%, CH 7]
    DIGEST:8h8h ID?8h8h8h8h STYPE:4h STARTUP:1b CH:3d 8h 8h8h 8h8h TEMP:12h TSIGN:1b ?1b BATT:1b ?1b MOIST:8h UV?~12h ?4h CHKSUM:8h

Moisture is transmitted in the humidity field as index 1-16: 0, 7, 13, 20, 27, 33, 40, 47, 53, 60, 67, 73, 80, 87, 93, 99.
The Wind speed and direction fields decode to valid zero but we exclude them from the output.

    aaaa2dd4e3ae1870079341ffffff0000221201fff279 [Batt ok]
    aaaa2dd43d2c1870079341ffffff0000219001fff2fc [Batt low]

    {206}55555555545ba83e803100058631ff11fe6611ffffffff01cc00 [Hum 96% Temp 3.8 C Wind 0.7 m/s]
    {205}55555555545ba999263100058631fffffe66d006092bffe0cff8 [Hum 95% Temp 3.0 C Wind 0.0 m/s]
    {199}55555555545ba840523100058631ff77fe668000495fff0bbe [Hum 95% Temp 3.0 C Wind 0.4 m/s]
    {205}55555555545ba94d063100058631fffffe665006092bffe14ff8
    {206}55555555545ba860703100058631fffffe6651ffffffff0135fc [Hum 95% Temp 3.0 C Wind 0.0 m/s]
    {205}55555555545ba924d23100058631ff99fe68b004e92dffe073f8 [Hum 96% Temp 2.7 C Wind 0.4 m/s]
    {202}55555555545ba813403100058631ff77fe6810050929ffe1180 [Hum 94% Temp 2.8 C Wind 0.4 m/s]
    {205}55555555545ba98be83100058631fffffe6130050929ffe17800 [Hum 95% Temp 2.8 C Wind 0.8 m/s]

    2dd4  1f 40 18 80 02 c3 18 ff 88 ff 33 08 ff ff ff ff 80 e6 00 [Hum 96% Temp 3.8 C Wind 0.7 m/s]
    2dd4  cc 93 18 80 02 c3 18 ff ff ff 33 68 03 04 95 ff f0 67 3f [Hum 95% Temp 3.0 C Wind 0.0 m/s]
    2dd4  20 29 18 80 02 c3 18 ff bb ff 33 40 00 24 af ff 85 df    [Hum 95% Temp 3.0 C Wind 0.4 m/s]
    2dd4  a6 83 18 80 02 c3 18 ff ff ff 33 28 03 04 95 ff f0 a7 3f
    2dd4  30 38 18 80 02 c3 18 ff ff ff 33 28 ff ff ff ff 80 9a 7f [Hum 95% Temp 3.0 C Wind 0.0 m/s]
    2dd4  92 69 18 80 02 c3 18 ff cc ff 34 58 02 74 96 ff f0 39 3f [Hum 96% Temp 2.7 C Wind 0.4 m/s]
    2dd4  09 a0 18 80 02 c3 18 ff bb ff 34 08 02 84 94 ff f0 8c 0  [Hum 94% Temp 2.8 C Wind 0.4 m/s]
    2dd4  c5 f4 18 80 02 c3 18 ff ff ff 30 98 02 84 94 ff f0 bc 00 [Hum 95% Temp 2.8 C Wind 0.8 m/s]

    {147} 5e aa 18 80 02 c3 18 fa 8f fb 27 68 11 84 81 ff f0 72 00 [Temp 11.8 C  Hum 81%]
    {149} ae d1 18 80 02 c3 18 fa 8d fb 26 78 ff ff ff fe 02 db f0
    {150} f8 2e 18 80 02 c3 18 fc c6 fd 26 38 11 84 81 ff f0 68 00 [Temp 11.8 C  Hum 81%]
    {149} c4 7d 18 80 02 c3 18 fc 78 fd 29 28 ff ff ff fe 03 97 f0
    {149} 28 1e 18 80 02 c3 18 fb b7 fc 26 58 ff ff ff fe 02 c3 f0
    {150} 21 e8 18 80 02 c3 18 fb 9c fc 33 08 11 84 81 ff f0 b7 f8 [Temp 11.8 C  Hum 81%]
    {149} 83 ae 18 80 02 c3 18 fc 78 fc 29 28 ff ff ff fe 03 98 00
    {150} 5c e4 18 80 02 c3 18 fb ba fc 26 98 11 84 81 ff f0 16 00 [Temp 11.8 C  Hum 81%]
    {148} d0 bd 18 80 02 c3 18 f9 ad fa 26 48 ff ff ff fe 02 ff f0

Wind and Temperature/Humidity or Rain:

    DIGEST:8h8h ID:8h8h8h8h STYPE:4h STARTUP:1b CH:3d WSPEED:~8h~4h ~4h~8h WDIR:12h ?4h TEMP:8h.4h TSIGN:1b ?1b BATT:1b ?1b HUM:8h UV?~12h ?4h CHKSUM:8h
    DIGEST:8h8h ID:8h8h8h8h STYPE:4h STARTUP:1b CH:3d WSPEED:~8h~4h ~4h~8h WDIR:12h ?4h RAINFLAG:8h RAIN:8h8h UV:8h8h CHKSUM:8h

Digest is LFSR-16 gen 0x8810 key 0x5412, excluding the add-checksum and trailer.
Checksum is 8-bit add (with carry) to 0xff.

Notes on different sensors:

- 1910 084d 18 : RebeckaJohansson, VENTUS W835
- 2030 088d 10 : mvdgrift, Wi-Fi Colour Weather Station with 5in1 Sensor, Art.No.: 7002580, ff 01 in the UV field is (obviously) invalid.
- 1970 0d57 18 : danrhjones, bresser 5-in-1 model 7002580, no UV
- 18b0 0301 18 : konserninjohtaja 6-in-1 outdoor sensor
- 18c0 0f10 18 : rege245 BRESSER-PC-Weather-station-with-6-in-1-outdoor-sensor
- 1880 02c3 18 : f4gqk 6-in-1
- 18b0 0887 18 : npkap
*/

static int bresser_6in1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0x2d, 0xd4};

    int const moisture_map[] = {0, 7, 13, 20, 27, 33, 40, 47, 53, 60, 67, 73, 80, 87, 93, 99}; // scale is 20/3

    data_t *data;
    uint8_t msg[18];

    if (bitbuffer->num_rows != 1
            || bitbuffer->bits_per_row[0] < 160
            || bitbuffer->bits_per_row[0] > 440) {
        decoder_logf(decoder, 2, __func__, "bit_per_row %u out of range", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_EARLY; // Unrecognized data
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof (preamble_pattern) * 8);

    if (start_pos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }
    start_pos += sizeof (preamble_pattern) * 8;

    unsigned len = bitbuffer->bits_per_row[0] - start_pos;
    if (len < sizeof(msg) * 8) {
        decoder_logf(decoder, 2, __func__, "%u too short", len);
        return DECODE_ABORT_LENGTH; // message too short
    }

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, sizeof(msg) * 8);

    decoder_log_bitrow(decoder, 2, __func__, msg, sizeof(msg) * 8, "");

    // LFSR-16 digest, generator 0x8810 init 0x5412
    int chkdgst = (msg[0] << 8) | msg[1];
    int digest  = lfsr_digest16(&msg[2], 15, 0x8810, 0x5412);
    if (chkdgst != digest) {
        decoder_logf(decoder, 2, __func__, "Digest check failed %04x vs %04x", chkdgst, digest);
        return DECODE_FAIL_MIC;
    }
    // Checksum, add with carry
    int chksum = msg[17];
    int sum    = add_bytes(&msg[2], 16); // msg[2] to msg[17]
    if ((sum & 0xff) != 0xff) {
        decoder_logf(decoder, 2, __func__, "Checksum failed %04x vs %04x", chksum, sum);
        return DECODE_FAIL_MIC;
    }

    uint32_t id = ((uint32_t)msg[2] << 24) | (msg[3] << 16) | (msg[4] << 8) | (msg[5]);
    int s_type  = (msg[6] >> 4); // 1: weather station, 2: indoor?, 3: pool thermometer, 4: soil probe
    int startup = (msg[6] >> 3) & 1; // s.a. #1214
    int chan    = (msg[6] & 0x7);
    int battery = (msg[13] >> 1) & 1; // b[13] & 0x02 is battery_good, s.a. #1993

    // temperature, humidity, shared with rain counter, only if valid BCD digits
    int temp_ok   = msg[12] <= 0x99 && (msg[13] & 0xf0) <= 0x90;
    int temp_raw  = (msg[12] >> 4) * 100 + (msg[12] & 0x0f) * 10 + (msg[13] >> 4);
    int temp_sign = (msg[13] >> 3) & 1;
    float temp_c  = temp_raw * 0.1f;
    if (temp_sign) {
        temp_c = (temp_raw - 1000) * 0.1f;
    }
    // Correction for Bresser 3-in-1 Professional Wind Gauge, PN 7002531
    if (temp_c < -50.0) {
        temp_c = -temp_raw * 0.1f;
    }

    int humidity    = (msg[14] >> 4) * 10 + (msg[14] & 0x0f);

    // apparently ff01 or 0000 if not available, ???0 if valid inverted BCD
    int uv_ok  = (msg[16] & 0x0f) == 0 && (~msg[15] & 0xff) <= 0x99 && (~msg[16] & 0xf0) <= 0x90;
    int uv_raw = ((~msg[15] & 0xf0) >> 4) * 100 + (~msg[15] & 0x0f) * 10 + ((~msg[16] & 0xf0) >> 4);
    float uv   = uv_raw * 0.1f;
    int flags  = (msg[16] & 0x0f); // looks like some flags, not sure

    //int unk_ok  = (msg[16] & 0xf0) == 0xf0;
    //int unk_raw = ((msg[15] & 0xf0) >> 4) * 10 + (msg[15] & 0x0f);

    // invert 3 bytes wind speeds
    msg[7] ^= 0xff;
    msg[8] ^= 0xff;
    msg[9] ^= 0xff;
    int wind_ok = (msg[7] <= 0x99) && (msg[8] <= 0x99) && (msg[9] <= 0x99);

    int gust_raw    = (msg[7] >> 4) * 100 + (msg[7] & 0x0f) * 10 + (msg[8] >> 4);
    float wind_gust = gust_raw * 0.1f;
    int wavg_raw    = (msg[9] >> 4) * 100 + (msg[9] & 0x0f) * 10 + (msg[8] & 0x0f);
    float wind_avg  = wavg_raw * 0.1f;
    int wind_dir    = ((msg[10] & 0xf0) >> 4) * 100 + (msg[10] & 0x0f) * 10 + ((msg[11] & 0xf0) >> 4);

    // rain counter, inverted 3 bytes BCD, shared with temp/hum, only if valid digits
    msg[12] ^= 0xff;
    msg[13] ^= 0xff;
    msg[14] ^= 0xff;
    int rain_ok   = msg[12] <= 0x99 && msg[13] <= 0x99 && msg[14] <= 0x99;
    int rain_raw  = (msg[12] >> 4) * 100000 + (msg[12] & 0x0f) * 10000
            + (msg[13] >> 4) * 1000 + (msg[13] & 0x0f) * 100
            + (msg[14] >> 4) * 10 + (msg[14] & 0x0f);
    float rain_mm = rain_raw * 0.1f;

    // the moisture sensor might present valid readings but does not have the hardware
    if (s_type == 4) {
        wind_ok = 0;
        uv_ok = 0;
    }

    int moisture = -1;
    if (s_type == 4 && temp_ok && humidity >= 1 && humidity <= 16)
        moisture = moisture_map[humidity - 1];

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Bresser-6in1",
            "id",               "",             DATA_FORMAT, "%08x", DATA_INT,    id,
            "channel",          "",             DATA_INT,    chan,
            "battery_ok",       "Battery",      DATA_COND, !rain_ok, DATA_INT,    battery,
            "temperature_C",    "Temperature",  DATA_COND, temp_ok, DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_COND, temp_ok && moisture < 0, DATA_INT,    humidity,
            "sensor_type",      "Sensor type",  DATA_INT,    s_type,
            "moisture",         "Moisture",     DATA_COND, moisture >= 0, DATA_FORMAT, "%d %%", DATA_INT, moisture,
            "wind_max_m_s",     "Wind Gust",    DATA_COND, wind_ok, DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind_gust,
            "wind_avg_m_s",     "Wind Speed",   DATA_COND, wind_ok, DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind_avg,
            "wind_dir_deg",     "Direction",    DATA_COND, wind_ok, DATA_INT,    wind_dir,
            "rain_mm",          "Rain",         DATA_COND, rain_ok, DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_mm,
            //"unknown",          "Unknown",      DATA_COND, unk_ok, DATA_INT,    unk_raw,
            "uv",               "UV",           DATA_COND, uv_ok, DATA_FORMAT, "%.1f", DATA_DOUBLE,    uv,
            "startup",          "Startup",      DATA_COND,   startup,   DATA_INT,    startup,
            "flags",            "Flags",        DATA_INT,    flags,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "sensor_type",
        "moisture",
        "wind_max_m_s",
        "wind_avg_m_s",
        "wind_dir_deg",
        "rain_mm",
        "uv",
        "startup",
        "flags",
        "mic",
        NULL,
};

r_device const bresser_6in1 = {
        .name        = "Bresser Weather Center 6-in-1, 7-in-1 indoor, soil, new 5-in-1, 3-in-1 wind gauge, Froggit WH6000, Ventus C8488A",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 124,
        .long_width  = 124,
        .reset_limit = 25000,
        .decode_fn   = &bresser_6in1_decode,
        .fields      = output_fields,
};
