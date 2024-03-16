/** @file
    Hideki Temperature, Humidity, Wind, Rain sensor.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Hideki Temperature, Humidity, Wind, Rain sensor.

Also: Bresser 5CH (Model 7009993)

The received bits are inverted.

Every 8 bits are stuffed with a (even) parity bit.
The payload (excluding the header) has an byte parity (XOR) check.
The payload (excluding the header) has CRC-8, poly 0x07 init 0x00 check.
The payload bytes are reflected (LSB first / LSB last) after the CRC check.

Temp:

    11111001 0  11110101 0  01110011 1 01111010 1  11001100 0  01000011 1  01000110 1  00111111 0  00001001 0  00010111 0
    SYNC+HEAD P   RC cha P     LEN   P     Nr.? P   .1° 1°  P   10°  BV P   1%  10% P     ?     P     XOR   P     CRC   P

TS04:

    00000000  11111111  22222222  33333333  44444444  55555555  66666666  77777777  88888888 99999999
    SYNC+HEAD cha   RC     LEN        Nr.?    1° .1°  VB   10°   10%  1%     ?         XOR      CRC

Wind:

    00000000  11111111  22222222  33333333  44444444  55555555  66666666  77777777  88888888 99999999 AAAAAAAA BBBBBBBB CCCCCCCC DDDDDDDD
    SYNC+HEAD cha   RC     LEN        Nr.?    1° .1°  VB   10°    1° .1°  VB   10°   1W .1W  .1G 10W   10G 1G    w°  AA    XOR      CRC

Rain:

    00000000  11111111  22222222  33333333  44444444  55555555  66666666  77777777  88888888
    SYNC+HEAD cha   RC   B LEN        Nr.?   RAIN_L    RAIN_H     0x66       XOR       CRC

*/

#include "decoder.h"

#define HIDEKI_MAX_BYTES_PER_ROW 14

enum sensortypes { HIDEKI_UNKNOWN, HIDEKI_TEMP, HIDEKI_TS04, HIDEKI_WIND, HIDEKI_RAIN };

static int hideki_ts04_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = 0;
    for (int row = 0; row < bitbuffer->num_rows; row++) {
        int sensortype;
        // Expect 8, 9, 10, or 14 unstuffed bytes, allow up to 4 missing bits
        int unstuffed_len = (bitbuffer->bits_per_row[row] + 4) / 9;
        if (unstuffed_len == 14)
            sensortype = HIDEKI_WIND;
        else if (unstuffed_len == 10)
            sensortype = HIDEKI_TS04;
        else if (unstuffed_len == 9)
            sensortype = HIDEKI_RAIN;
        else if (unstuffed_len == 8)
            sensortype = HIDEKI_TEMP;
        else {
            ret = DECODE_ABORT_LENGTH;
            continue;
        }
        unstuffed_len -= 1; // exclude sync

        uint8_t *b = bitbuffer->bb[row];
        // Expect a start (not inverted) of 00000110 1, but allow missing bits
        int sync = b[0] << 1 | b[1] >> 7;
        int startpos = -1;
        for (int i = 0; i < 4; ++i) {
            if (sync == 0x0d) {
                startpos = 9 - i;
                break;
            }
            sync >>= 1;
        }
        if (startpos < 0) {
            ret = DECODE_ABORT_EARLY;
            continue;
        }

        // Invert all bits
        bitbuffer_invert(bitbuffer);

        uint8_t packet[HIDEKI_MAX_BYTES_PER_ROW];
        // Strip (unstuff) and check parity bit
        // TODO: refactor to util function
        int unstuff_error = 0;
        for (int i = 0; i < unstuffed_len; ++i) {
            unsigned int offset = startpos + i * 9;
            packet[i] = (b[offset / 8] << (offset % 8)) | (b[offset / 8 + 1] >> (8 - offset % 8));
            // check parity
            uint8_t parity = (b[offset / 8 + 1] >> (7 - offset % 8)) & 1;
            if (parity != parity8(packet[i])) {
                decoder_logf(decoder, 1, __func__, "Parity error at %d", i);
                ret = DECODE_FAIL_MIC;
                unstuff_error = i;
                break;
            }
        }
        if (unstuff_error) {
            continue;
        }

        // XOR check all bytes
        int chk = xor_bytes(packet, unstuffed_len - 1);
        if (chk) {
            decoder_log(decoder, 1, __func__, "XOR error");
            ret = DECODE_FAIL_MIC;
            continue;
        }

        // CRC-8 poly=0x07 init=0x00
        if (crc8(packet, unstuffed_len, 0x07, 0x00)) {
            decoder_log(decoder, 1, __func__, "CRC error");
            ret = DECODE_FAIL_MIC;
            continue;
        }

        // Reflect LSB first to LSB last
        reflect_bytes(packet, unstuffed_len);

        int pkt_len  = (packet[1] >> 1) & 0x1f;
        //int pkt_seq  = packet[2] >> 6;
        //int pkt_type = packet[2] & 0x1f;
        // 0x0C Anemometer
        // 0x0D UV sensor
        // 0x0E Rain level meter
        // 0x1E Thermo/hygro-sensor

        if (pkt_len + 2 != unstuffed_len) {
            decoder_log(decoder, 1, __func__, "LEN error");
            ret = DECODE_ABORT_LENGTH;
            continue;
        }

        int channel = (packet[0] >> 5) & 0x0F;
        if (channel >= 5) channel -= 1;
        int rc = packet[0] & 0x0F;
        int temp = (packet[4] & 0x0F) * 100 + ((packet[3] & 0xF0) >> 4) * 10 + (packet[3] & 0x0F);
        if (((packet[4]>>7) & 1) == 0) {
            temp = -temp;
        }
        int battery_ok = (packet[4] >> 6) & 1;

        if (sensortype == HIDEKI_TS04) {
            int humidity = ((packet[5] & 0xF0) >> 4) * 10 + (packet[5] & 0x0F);
            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",                 DATA_STRING, "Hideki-TS04",
                    "id",               "Rolling Code",     DATA_INT,    rc,
                    "channel",          "Channel",          DATA_INT,    channel,
                    "battery_ok",       "Battery",          DATA_INT,    battery_ok,
                    "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp/10.f,
                    "humidity",         "Humidity",         DATA_FORMAT, "%u %%", DATA_INT, humidity,
                    "mic",              "Integrity",        DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            return 1;
        }
        if (sensortype == HIDEKI_WIND) {
            int const wd[] = { 0, 15, 13, 14, 9, 10, 12, 11, 1, 2, 4, 3, 8, 7, 5, 6 };
            int wind_direction = wd[((packet[10] & 0xF0) >> 4)] * 225;
            int wind_speed = (packet[8] & 0x0F) * 100 + (packet[7] >> 4) * 10 + (packet[7] & 0x0F);
            int gust_speed = (packet[9] >> 4) * 100 + (packet[9] & 0x0F) * 10 + (packet[8] >> 4);
            int const ad[] = { 0, 1, -1, 2 }; // i.e. None, CW, CCW, invalid
            int wind_approach = ad[(packet[10] >> 2) & 0x03];

            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",                 DATA_STRING, "Hideki-Wind",
                    "id",               "Rolling Code",     DATA_INT,    rc,
                    "channel",          "Channel",          DATA_INT,    channel,
                    "battery_ok",       "Battery",          DATA_INT,    battery_ok,
                    "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp * 0.1f,
                    "wind_avg_mi_h",    "Wind Speed",       DATA_FORMAT, "%.2f mi/h", DATA_DOUBLE, wind_speed * 0.1f,
                    "wind_max_mi_h",    "Gust Speed",       DATA_FORMAT, "%.2f mi/h", DATA_DOUBLE, gust_speed * 0.1f,
                    "wind_approach",    "Wind Approach",    DATA_INT,    wind_approach,
                    "wind_dir_deg",     "Wind Direction",   DATA_FORMAT, "%.1f", DATA_DOUBLE, wind_direction * 0.1f,
                    "mic",              "Integrity",        DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            return 1;
        }
        if (sensortype == HIDEKI_TEMP) {
            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",                 DATA_STRING, "Hideki-Temperature",
                    "id",               "Rolling Code",     DATA_INT,    rc,
                    "channel",          "Channel",          DATA_INT,    channel,
                    "battery_ok",       "Battery",          DATA_INT,    battery_ok,
                    "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp * 0.1f,
                    "mic",              "Integrity",        DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            return 1;
        }
        if (sensortype == HIDEKI_RAIN) {
            int rain_units = (packet[4] << 8) | packet[3];
            battery_ok = (packet[1] >> 6) & 1;

            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",                 DATA_STRING, "Hideki-Rain",
                    "id",               "Rolling Code",     DATA_INT,    rc,
                    "channel",          "Channel",          DATA_INT,    channel,
                    "battery_ok",       "Battery",          DATA_INT,    battery_ok,
                    "rain_mm",          "Rain",             DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_units * 0.7f,
                    "mic",              "Integrity",        DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            return 1;
        }
        // unknown sensor type
        return DECODE_FAIL_SANITY;
    }
    return ret;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "wind_avg_mi_h",
        "wind_max_mi_h",
        "wind_approach",
        "wind_dir_deg",
        "rain_mm",
        "mic",
        NULL,
};

r_device const hideki_ts04 = {
        .name        = "HIDEKI TS04 Temperature, Humidity, Wind and Rain Sensor",
        .modulation  = OOK_PULSE_DMC,
        .short_width = 520,  // half-bit width 520 us
        .long_width  = 1040, // bit width 1040 us
        .reset_limit = 4000,
        .tolerance   = 240,
        .decode_fn   = &hideki_ts04_decode,
        .fields      = output_fields,
};
