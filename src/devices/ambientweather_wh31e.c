/** @file
    Ambient Weather WH31E, EcoWitt WH40 protocol.

    Copyright (C) 2018 Christian W. Zuckschwerdt <zany@triq.net>
    based on protocol analysis by James Cuff and Michele Clamp,
    EcoWitt WH40 analysis by Helmut Bachmann,
    Ecowitt WS68 analysis by Tolip Wen.
    EcoWitt WH31B analysis by Michael Turk.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Ambient Weather WH31E protocol.
915 MHz FSK PCM Thermo-Hygrometer Sensor (bundled as Ambient Weather WS-3000-X5).

Note that Ambient Weather and EcoWitt are likely rebranded Fine Offset products.

56 us bit length with a warm-up of 1336 us mark(pulse), 1996 us space(gap),
a preamble of 48 bit flips (0xaaaaaaaaaaaa) and a 0x2dd4 sync-word.

Data layout:

    YY II CT TT HH XX AA ?? ?? ?? ??

- Y is a fixed Type Code of 0x30
- I is a device ID
- C is 6 bits Channel number (3 bits) and flags: "1CCC0B"
- T is 10 bits Temperature in C, scaled by 10, offset 400
- H is Humidity
- X is CRC-8, poly 0x31, init 0x00
- A is SUM-8

Data decoding:

    TYPE:8h ID:8h ?1b CH:3b ?1b BATT:1b TEMP:10d HUM:8d CRC:8h SUM:8h ?8h8h8h8h

Example packets:

    {177} aa aa aa aa aa aa  2d d4  30 c3 8 20a 5e  df bc   07 56 a7 ae  00 00 00 00
    {178} aa aa aa aa aa aa  2d d4  30 44 9 21a 39  5a b3   07 45 04 5f  00 00 00 00

Some payloads:

    30 c3 81 d5 5c 2a cf 08 35 44 2c
    30 35 c2 2f 3c 0f a1 07 52 29 9f
    30 35 c2 2e 3c fb 8c 07 52 29 9f
    30 c9 a2 1e 40 0c 05 07 34 c6 b1
    30 2b b2 14 3d 94 f2 08 53 78 e6
    30 c9 a2 1f 40 f8 f2 07 34 c6 b1
    30 44 92 13 3e 0e 65 07 45 04 5f
    30 44 92 15 3d 07 5f 07 45 04 5f
    30 c3 81 d6 5b 90 35 08 35 44 2c


Ambient Weather WH31E Radio Controlled Clock (RCC) packet WWVB

These packets are sent with this schedule, according to the manual:
    After the remote sensor is powered up, the sensor will transmit weather
    data for 30 seconds, and then the sensor will begin radio controlled clock
    (RCC) reception. During the RCC time reception period (maximum 5 minutes),
    no weather data will be transmitted to avoid interference.

    If the signal reception is not successful within 3 minute, the signal
    search will be cancelled and will automatically resume every two hours
    until the signal is successfully captured. The regular RF link will resume
    once RCC reception routine is finished.

 / time message type 0x52
 |  / station id
 |  |  / unknown
 |  |  |  / 20xx year in BCD
 |  |  |  |  / month in BCD
 |  |  |  |  |  / day in BCD
 |  |  |  |  |  |  / hour in BCD
 |  |  |  |  |  |  |  / minute in BCD
 |  |  |  |  |  |  |  |  / second in BCD
 |  |  |  |  |  |  |  |  |  / CRC-8, poly 0x31, init 0x00
 |  |  |  |  |  |  |  |  |  |  / SUM-8
YY II UU YY MM DD HH mm SS CC XX
 0  1  2  3  4  5  6  7  8  9 10 - byte index

UU has kept the value 0x4a.  Data it may represent that is broadcast from WWVB:
- Daylight savings upcoming/active (it WAS active during the captures) (2 bits)
- Leap year (1 bit)
- Leap second at the end of this month (1 bit)
- DUT1, difference between UTC and UT1 (4-7 bits depending on re-encoding)
The upper bits of the upper nibbles M, D, H, m, S may possibly be used to
encode this information, given their maximum valid digits of 1, 3, 2, 6, 6,
respectively.

Packets observed
Reception time               Payload
2020-10-20T02:06:55.809Z  52 27 4a 20 10 20 02 06 55 05 75
2020-10-20T02:08:02.793Z  52 27 4a 20 10 20 02 08 02 81 a0
2020-10-20T07:35:04.290Z  52 75 4a 20 10 20 07 35 03 8a 2a
2020-10-20T07:35:52.394Z  52 58 4a 20 10 20 07 35 51 48 19
2020-10-20T07:36:06.287Z  52 75 4a 20 10 20 07 36 05 01 a4
2020-10-20T07:36:55.305Z  52 58 4a 20 10 20 07 36 54 90 65
2020-10-20T07:37:08.284Z  52 75 4a 20 10 20 07 37 07 97 3d
2020-10-20T07:37:58.355Z  52 58 4a 20 10 20 07 37 57 37 10
2020-10-20T07:38:10.280Z  52 75 4a 20 10 20 07 38 09 11 ba
2020-10-20T07:39:01.398Z  52 58 4a 20 10 20 07 39 00 b3 37
2020-10-20T08:05:50.830Z  52 a0 4a 20 10 20 08 05 50 0f f8
2020-10-20T08:06:58.862Z  52 a0 4a 20 10 20 08 06 58 9b 8d
2020-10-20T08:08:06.883Z  52 a0 4a 20 10 20 08 08 06 97 39
2020-10-20T08:09:14.785Z  52 a0 4a 20 10 20 08 09 14 42 f3


EcoWitt WH40 protocol.
Seems to be the same as Fine Offset WH5360 or Ecowitt WH5360B.

Data layout:

    YY 00 IIII FV RRRR XX AA 00 02 ?? 00 00

- Y is a fixed Type Code of 0x40
- I is a device ID
- F is perhaps flags, but only seen fixed 0x10 so far
- V is battery voltage, ( FV & 0x1f ) * 0.1f
- R is the rain bucket tip count, 0.1mm increments
- X is CRC-8, poly 0x31, init 0x00
- A is SUM-8

Some payloads:

    4000 cd6f 10 0000  64 f0 ; 00 027b 0000
    4000 cd6f 10 0001  55 e2 ; 00 02f6 0000
    4000 cd6f 10 0002  06 94 ; 00 02ed 0000
    4000 cd6f 10 0003  37 c6 ; 00 02db 0000
    4000 cd6f 10 0004  a0 30 ; 00 02b7 0000
    4000 cd6f 10 0005  91 22 ; 00 02de 0000
    4000 cd6f 10 0006  c2 54 ; 00 02bd 0000
    4000 cd6f 10 0007  f3 86 ; 00 027b 0000
    4000 cd6f 10 0008  dd 71 ; 00 02f6 0000
    4000 cd6f 10 0009  ec 81 ; 00 02ed 0000
    4000 cd6f 10 000a  bf 55 ; 00 02db 0000

Samples with 1.2V battery (last 2 samples contain 1 manual bucket tip)

    4000 cd6f 10 0000  64 f0 ; 00 01de 00b0
    4000 cd6f 10 0000  64 f0 ; 00 02de 00b0
    4000 cd6f 10 0000  64 f0 ; 00 02bd 0000
    4000 cd6f 10 0001  55 e2 ; 00 027b 0000
    4000 cd6f 10 0001  55 e2 ; 00 027b 0000

Samples with 0.9V battery (last 3 samples contain 1 manual bucket tip)

    4000 cd6f 10 0000  64 f0 ; 00 16de 0000
    4000 cd6f 10 0000  64 f0 ; 00 02de 0000
    4000 cd6f 10 0001  55 e2 ; 00 02bd 0000
    4000 cd6f 10 0001  55 e2 ; 00 027b 0000
    4000 cd6f 10 0001  55 e2 ; 00 027b 0000

Ecowitt WS68 Anemometer protocol.

Data layout:

    TYPE:8h ?8h ID:16h LUX:16h BATT:8h WDIR_H:4h 4h8h8h WSPEED:8h WDIR_LO:8h WGUST:8h ?8h CRC:8h SUM:8h ?8h4h

Some payloads:

    68 0000 c5 0000 4b 0f ffff 00 5a 00 00 d0af 104
    68 0000 c5 0000 4b 0f ffff 00 b4 00 00 79b2 102
    68 0000 c5 0000 4b 0f ffff 7e e0 94 00 75ec 102
    68 0000 c5 0000 4b 2f ffff 00 0e 00 00 8033 208
    68 0000 c5 000f 4b 0f ffff 00 2e 00 00 d395 108
    68 0000 c5 0107 4b 0f ffff 00 2e 00 02 a663 100

*/

#include "decoder.h"

static int ambientweather_whx_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int events = 0;
    uint8_t b[18]; // actually only 6/9/17.5 bytes, no indication what the last 5 might be
    int row;
    int msg_type;
    uint8_t const wh31e_type_code = 0x30; // 48
    uint8_t const wh31b_type_code = 0x37; // 55

    uint8_t const preamble[] = {0xaa, 0x2d, 0xd4}; // (partial) preamble and sync word

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        // Validate message and reject it as fast as possible : check for preamble
        unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, 24);
        // no preamble detected, move to the next row
        if (start_pos == bitbuffer->bits_per_row[row])
            continue; // DECODE_ABORT_EARLY
        decoder_logf(decoder, 1, __func__, "WH31E/WH31B/WH40 detected, buffer is %u bits length", bitbuffer->bits_per_row[row]);

        // remove preamble, keep whole payload
        bitbuffer_extract_bytes(bitbuffer, row, start_pos + 24, b, 18 * 8);
        msg_type = b[0];

        if (msg_type == wh31e_type_code || msg_type == wh31b_type_code) {
            uint8_t c_crc = crc8(b, 6, 0x31, 0x00);
            if (c_crc) {
                decoder_logf(decoder, 1, __func__, "WH31E/WH31B (%d) bad CRC", msg_type);
                continue; // DECODE_FAIL_MIC
            }
            uint8_t c_sum = add_bytes(b, 6) - b[6];
            if (c_sum) {
                decoder_logf(decoder, 1, __func__, "WH31E/WH31B (%d) bad SUM", msg_type);
                continue; // DECODE_FAIL_MIC
            }

            int id       = b[1];
            int batt_low = ((b[2] & 0x04) >> 2);
            int channel  = ((b[2] & 0x70) >> 4) + 1;
            int temp_raw = ((b[2] & 0x03) << 8) | (b[3]);
            float temp_c = (temp_raw - 400) * 0.1f;
            int humidity = b[4];
            char extra[11];
            snprintf(extra, sizeof(extra), "%02x%02x%02x%02x%02x", b[6], b[7], b[8], b[9], b[10]);

            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",             DATA_COND, msg_type == 0x30, DATA_STRING, "AmbientWeather-WH31E",
                    "model",            "",             DATA_COND, msg_type == 0x37, DATA_STRING, "AmbientWeather-WH31B",
                    "id" ,              "",             DATA_INT,    id,
                    "channel",          "Channel",      DATA_INT,    channel,
                    "battery_ok",       "Battery",      DATA_INT,    !batt_low,
                    "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                    "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
                    "data",             "Extra Data",   DATA_STRING, extra,
                    "mic",              "Integrity",    DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            events++;
        }

        else if (msg_type == 0x52) {
            // WH31E (others?) RCC
            uint8_t c_crc = crc8(b, 10, 0x31, 0x00);
            if (c_crc) {
                decoder_log(decoder, 1, __func__, "WH31E RCC bad CRC");
                continue; // DECODE_FAIL_MIC
            }
            uint8_t c_sum = add_bytes(b, 10) - b[10];
            if (c_sum) {
                decoder_log(decoder, 1, __func__, "WH31E RCC bad SUM");
                continue; // DECODE_FAIL_MIC
            }

            int id      = b[1];
            int unknown = b[2];
            int year    = ((b[3] & 0xF0) >> 4) * 10 + (b[3] & 0x0F) + 2000;
            int month   = ((b[4] & 0x10) >> 4) * 10 + (b[4] & 0x0F);
            int day     = ((b[5] & 0x30) >> 4) * 10 + (b[5] & 0x0F);
            int hours   = ((b[6] & 0x30) >> 4) * 10 + (b[6] & 0x0F);
            int minutes = ((b[7] & 0x70) >> 4) * 10 + (b[7] & 0x0F);
            int seconds = ((b[8] & 0x70) >> 4) * 10 + (b[8] & 0x0F);

            char clock_str[23];
            snprintf(clock_str, sizeof(clock_str), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                    year, month, day, hours, minutes, seconds);

            /* clang-format off */
            data_t *data = data_make(
                    "model",        "",             DATA_STRING,    "AmbientWeather-WH31E",
                    "id" ,          "Station ID",   DATA_INT,       id,
                    "data",         "Unknown",      DATA_INT,       unknown,
                    "radio_clock",  "Radio Clock",  DATA_STRING,    clock_str,
                    "mic",          "Integrity",    DATA_STRING,    "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            events++;
        }

        else if (msg_type == 0x40) {
            // WH40
            uint8_t c_crc = crc8(b, 8, 0x31, 0x00);
            if (c_crc) {
                decoder_log(decoder, 1, __func__, "WH40 bad CRC");
                continue; // DECODE_FAIL_MIC
            }
            uint8_t c_sum = add_bytes(b, 8) - b[8];
            if (c_sum) {
                decoder_log(decoder, 1, __func__, "WH40 bad SUM");
                continue; // DECODE_FAIL_MIC
            }

            int id         = (b[2] << 8) | b[3];
            int battery_v  = (b[4] & 0x1f);
            int battery_lvl = battery_v <= 9 ? 0 : ((battery_v - 9) / 6 * 100); // 0.9V-1.5V is 0-100
            int rain_raw   = (b[5] << 8) | b[6];
            char extra[11];
            snprintf(extra, sizeof(extra), "%02x%02x%02x%02x%02x", b[9], b[10], b[11], b[12], b[13]);

            if (battery_lvl > 100)
                battery_lvl = 100;

            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",                DATA_STRING, "EcoWitt-WH40",
                    "id" ,              "",                DATA_INT,    id,
                    "battery_V",        "Battery Voltage", DATA_COND, battery_v != 0, DATA_FORMAT, "%f V", DATA_DOUBLE, battery_v * 0.1f,
                    "battery_ok",       "Battery",         DATA_COND, battery_v != 0, DATA_DOUBLE, battery_lvl * 0.01f,
                    "rain_mm",          "Total Rain",      DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_raw * 0.1,
                    "data",             "Extra Data",      DATA_STRING, extra,
                    "mic",              "Integrity",       DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            events++;
        }

        else if (msg_type == 0x68) {
            // WS68
            uint8_t c_crc = crc8(b, 15, 0x31, 0x00);
            if (c_crc) {
                decoder_log(decoder, 1, __func__, "WH68 bad CRC");
                continue; // DECODE_FAIL_MIC
            }
            uint8_t c_sum = add_bytes(b, 15) - b[15];
            if (c_sum) {
                decoder_log(decoder, 1, __func__, "WH68 bad SUM");
                continue; // DECODE_FAIL_MIC
            }

            int id      = (b[2] << 8) | b[3];
            int lux     = (b[4] << 8) | b[5];
            int batt    = b[6];
            int batt_ok = batt > 0x30; // wild guess
            int wspeed  = b[10];
            int wgust   = b[12];
            int wdir    = ((b[7] & 0x20) >> 5) | b[11];
            char extra[7];
            snprintf(extra, sizeof(extra), "%02x %02x%01x", b[13], b[16], b[17] >> 4);

            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",             DATA_STRING, "EcoWitt-WS68",
                    "id" ,              "",             DATA_INT,    id,
                    "battery_raw",      "Battery Raw",  DATA_INT,    batt,
                    "battery_ok",       "Battery",      DATA_INT,    batt_ok,
                    "lux_raw",          "lux",          DATA_INT,    lux,
                    "wind_avg_raw",     "Wind Speed",   DATA_INT,    wspeed,
                    "wind_max_raw",     "Wind Gust",    DATA_INT,    wgust,
                    "wind_dir_deg",     "Wind dir",     DATA_INT,    wdir,
                    "data",             "Extra Data",   DATA_STRING, extra,
                    "mic",              "Integrity",    DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            events++;
        }

        else {
            decoder_logf(decoder, 1, __func__, "unknown message type %02x (expected 0x30/0x40/0x68)", msg_type);
        }
    }
    return events;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "battery_v",
        "temperature_C",
        "humidity",
        "rain_mm",
        "lux",
        "wind_avg_km_h",
        "wind_max_km_h",
        "wind_dir_deg",
        "data",
        "radio_clock",
        "mic",
        NULL,
};

r_device const ambientweather_wh31e = {
        .name        = "Ambient Weather WH31E Thermo-Hygrometer Sensor, EcoWitt WH40 rain gauge",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 56,
        .long_width  = 56,
        .reset_limit = 1500,
        .gap_limit   = 1800,
        .decode_fn   = &ambientweather_whx_decode,
        .fields      = output_fields,
};
