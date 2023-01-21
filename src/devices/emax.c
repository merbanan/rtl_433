/** @file
    First version was for Altronics X7064 temperature and humidity sensor.
    Then updated by Profboc75 with Optex 990040 (Emax full Weather station rain gauge/wind speed/wind direction ... ref EM3390W6 )

    Copyright (C) 2022 Christian W. Zuckschwerdt <zany@triq.net>
    based on protocol decoding by Thomas White

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Fuzhou Emax Electronic W6 Professional Weather Station.
Rebrand and devices decoded :
- Emax W6 / WEC-W6 / 3390TX W6 / EM3390W6
- Altronics x7063/4
- Optex 990040 / 990050 / 990051 / SM-040
- Infactory FWS-1200
- Newentor Q9
- Otio Weather Station Pro La Surprenante 810025
- Orium Pro Atlanta 13093, Helios 13123
- Protmex PT3390A
- Jula Marquant 014331 weather station /014332 temp hum sensor

S.a. issue #2000 #2299 #2326  PR #2300

- Likely a rebranded device, sold by Altronics
- Data length is 32 bytes with a preamble of 10 bytes (33 bytes for Rain/Wind Station)

Data Layout:

    // That fits nicely: aaa16e95 a3 8a ae 2d is channel 1, id 6e95, temp 38e (=910, 1 F, -17.2 C), hum 2d (=45).

Temp/Hum Sensor :
    AA AC II IB AT TA AT HH AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA SS

default empty = 0xAA

- K: (4 bit) Kind of device, = A if Temp/Hum Sensor or = 0 if Weather Rain/Wind station
- C: (4 bit) channel ( = 4 for Weather Rain/wind station)
- I: (12 bit) ID
- B: (4 bit) BP01: battery low, pairing button, 0, 1
- T: (12 bit) temperature in F, offset 900, scale 10
- H: (8 bit) humidity %
- A: (4 bit) fixed values of 0xA
- S: (8 bit) checksum

Raw data:

    FF FF AA AA AA AA AA CA CA 54
    AA A1 6E 95 A6 BA A5 3B AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA AA D4
    AA 00 0

Format string:

    12h CH:4h ID:12h FLAGS:4b TEMP:4x4h4h4x4x4h HUM:8d 184h CHKSUM:8h 8x

Decoded example:

    aaa CH:1 ID:6e9 FLAGS:0101 TEMP:6b5 HUM:059 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa CHKSUM:d4 000


Emax EM3390W6 Rain / Wind speed / Wind Direction / Temp / Hum / UV / Lux

Weather Rain/Wind station : hummidity not at same byte position.
    AA 04 II IB 0T TT HH 0W WW 0D DD RR RR 0U LL LL 04 05 06 07 08 09 10 11 12 13 14 15 16 17 xx SS yy

default empty/null = 0x01 => value = 0

- K: (4 bit) Kind of device, = A if Temp/Hum Sensor or = 0 if Weather Rain/Wind station
- C: (4 bit) channel ( = 4 for Weather Rain/wind station)
- I: (12 bit) ID
- B: (4 bit) BP01: battery low, pairing button, 0, 1
- T: (12 bit) temperature in F, offset 900, scale 10
- H: (8 bit) humidity %
- R: (16) Rain
- W: (12) Wind speed
- D: (9 bit) Wind Direction
- U: (5 bit) UV index
- L: (1 + 15 bit) Lux value, if first bit = 1 , then x 10 the rest.
- A: (4 bit) fixed values of 0xA
- 0: (4 bit) fixed values of 0x0
- xx: incremental value each tx
- yy: incremental value each tx yy = xx + 1
- S: (8 bit) checksum

Raw Data:

    ff ff 80 00 aa aa aa aa aa ca ca 54
    aa 04 59 41 06 1f 42 01 01 01 81 01 16 01 01 01 04 05 06 07 08 09 10 11 12 13 14 15 16 17 9d ad 9e
    0000

Format string:

    8h K:4h CH:4h ID:12h Flags:4b 4h Temp:12h Hum:8h 4h Wind:12h 4h Direction: 12h Rain: 16h 4h UV:4h Lux:16h  112h xx:8d CHKSUM:8h

Decoded example:

    aa KD:0 CH:4 ID:594 FLAGS:0001 0 TEMP:61f (66.7F) HUM:42 (66%) Wind: 101 ( = 000 * 0.2 = 0 kmh) 0 Direction: 181 ( = 080 = 128°) Rain: 0116 ( 0015 * 0.2  = 4.2 mm) 0 UV: 1 (0 UV) Lux: 0101 (0 Lux) 04 05 ...16 17 xx:9d CHKSUM:ad yy:9e

*/

static int emax_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is ffffaaaaaaaaaacaca54
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0xca, 0xca, 0x54};

    int ret = 0;
    for (unsigned row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned pos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, sizeof(preamble_pattern) * 8);

        if (pos >= bitbuffer->bits_per_row[row]) {
            decoder_log(decoder, 2, __func__, "Preamble not found");
            ret = DECODE_ABORT_EARLY;
            continue;
        }
        decoder_logf(decoder, 2, __func__, "Found row: %d", row);

        pos += sizeof(preamble_pattern) * 8;
        // we expect 32 bytes
        if (pos + 32 * 8 > bitbuffer->bits_per_row[row]) {
            decoder_log(decoder, 2, __func__, "Length check fail");
            ret = DECODE_ABORT_LENGTH;
            continue;
        }
        uint8_t b[32] = {0};
        bitbuffer_extract_bytes(bitbuffer, row, pos, b, sizeof(b) * 8);

        // verify checksum
        if ((add_bytes(b, 31) & 0xff) != b[31]) {
            decoder_log(decoder, 2, __func__, "Checksum fail");
            ret = DECODE_FAIL_MIC;
            continue;
        }

        int channel     = (b[1] & 0x0f);
        int kind        = ((b[1] & 0xf0) >> 4);
        int id          = (b[2] << 4) | (b[3] >> 4);
        int battery_low = (b[3] & 0x08);
        int pairing     = (b[3] & 0x04);

        // depend if external temp/hum sensor or Weather rain/wind station the values are not decode the same

        if (kind != 0) {  // if not Rain/Wind ... sensor

            int temp_raw    = ((b[4] & 0x0f) << 8) | (b[5] & 0xf0) | (b[6] & 0x0f); // weird format
            float temp_f    = (temp_raw - 900) * 0.1f;
            int humidity    = b[7];

            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",                 DATA_STRING, "Altronics-X7064",
                    "id",               "",                 DATA_FORMAT, "%03x", DATA_INT,    id,
                    "channel",          "Channel",          DATA_INT,    channel,
                    "battery_ok",       "Battery_OK",       DATA_INT,    !battery_low,
                    "temperature_F",    "Temperature_F",    DATA_FORMAT, "%.1f", DATA_DOUBLE, temp_f,
                    "humidity",         "Humidity",         DATA_FORMAT, "%u", DATA_INT, humidity,
                    "pairing",          "Pairing?",         DATA_COND,   pairing,   DATA_INT,    !!pairing,
                    "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            return 1;
        }
        else {  // if Rain/Wind sensor

            int temp_raw      = ((b[4] & 0x0f) << 8) | (b[5]); // weird format
            float temp_f      = (temp_raw - 900) * 0.1f;
            int humidity      = b[6];
            int wind_raw      = ((b[7] - 1) << 8 ) | (b[8] -1);   // all default is 01 so need to remove 1 from all bytes.
            float speed_kmh     = wind_raw * 0.2f;
            int direction_deg = (((b[9] & 0x0f) - 1) << 8) | (b[10] - 1);
            int rain_raw      = ((b[11] - 1) << 8 ) | (b[12] -1);
            float rain_mm     = rain_raw * 0.2f;
            int uv_index      = (b[13] & 0x1f) - 1;
            int lux_multi     = (((b[14] - 1) & 0x80) >> 7);
            int light_lux     = ((b[14] & 0x7f) - 1) << 8 | (b[15] - 1);
            if (lux_multi == 1) {
                light_lux = light_lux * 10;
            }

            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",                 DATA_STRING, "Emax-W6",
                    "id",               "",                 DATA_FORMAT, "%03x", DATA_INT,    id,
                    "channel",          "Channel",          DATA_INT,    channel,
                    "battery_ok",       "Battery_OK",       DATA_INT,    !battery_low,
                    "temperature_F",    "Temperature_F",    DATA_FORMAT, "%.1f", DATA_DOUBLE, temp_f,
                    "humidity",         "Humidity",         DATA_FORMAT, "%u",   DATA_INT,    humidity,
                    "wind_avg_km_h",    "Wind avg speed",   DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, speed_kmh,
                    "wind_dir_deg",     "Wind Direction",   DATA_INT,    direction_deg,
                    "rain_mm",          "Total rainfall",   DATA_FORMAT, "%.1f mm",  DATA_DOUBLE, rain_mm,
                    "uv",               "UV Index",         DATA_FORMAT, "%u",       DATA_INT,    uv_index,
                    "light_lux",        "Lux",              DATA_FORMAT, "%u",       DATA_INT,    light_lux,
                    "pairing",          "Pairing?",         DATA_COND,   pairing,    DATA_INT,    !!pairing,
                    "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            return 1;

        }
    }
    return ret;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_F",
        "humidity",
        "wind_avg_km_h",
        "rain_mm",
        "wind_dir_deg",
        "uv",
        "light_lux",
        "pairing",
        "mic",
        NULL,
};

r_device emax = {
        .name        = "Emax W6, rebrand Altronics x7063/4, Optex 990040/50/51, Orium 13093/13123, Infactory FWS-1200, Newentor Q9, Otio 810025, Protmex PT3390A, Jula Marquant 014331/32, Weather Station or temperature/humidity sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 90,
        .long_width  = 90,
        .gap_limit   = 1200,
        .reset_limit = 9000,
        .decode_fn   = &emax_decode,
        .fields      = output_fields,
};
