/** @file
    LaCrosse WS-2310 / WS-3600 433 Mhz Weather Station.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/** @fn int lacrossews_callback(r_device *decoder, bitbuffer_t *bitbuffer)
LaCrosse WS-2310 / WS-3600 433 Mhz Weather Station.

- long pulse 1464 us
- short pulse 368 us
- fixed gap 1336 us

Packet Format is 53 bits/ 13 nibbles:

|  bits | nibble
| ----- | ------
|  0- 3 | 0 - 0000
|  4- 7 | 1 - 1001 for WS-2310, 0110 for WS-3600
|  8-11 | 2 - Type  GPTT  G=0, P=Parity, Gust=Gust, TT=Type  GTT 000=Temp, 001=Humidity, 010=Rain, 011=Wind, 111-Gust
| 12-15 | 3 - ID High
| 16-19 | 4 - ID Low
| 20-23 | 5 - Data Types  GWRH  G=Gust Sent, W=Wind Sent, R=Rain Sent, H=Humidity Sent
| 24-27 | 6 - Parity TUU? T=Temp Sent, UU=Next Update, 00=8 seconds, 01=32 seconds, 10=?, 11=128 seconds, ?=?
| 28-31 | 7 - Value1
| 32-35 | 8 - Value2
| 36-39 | 9 - Value3
| 40-43 | 10 - ~Value1
| 44-47 | 11 - ~Value2
| 48-51 | 12 - Check Sum = Nibble sum of nibbles 0-11
*/

#include "decoder.h"

#define LACROSSE_WS_BITLEN 52

static int lacrossews_detect(r_device *decoder, uint8_t *pRow, uint8_t *msg_nybbles, int16_t rowlen)
{
    int i;
    uint8_t rbyte_no, rbit_no, mnybble_no, mbit_no;
    uint8_t bit, checksum = 0, parity = 0;

    // Weather Station 2310 Packets
    if (rowlen != LACROSSE_WS_BITLEN)
        return DECODE_ABORT_LENGTH;
    if (pRow[0] != 0x09 && pRow[0] != 0x06)
        return DECODE_ABORT_EARLY;

    for (i = 0; i < (LACROSSE_WS_BITLEN / 4); i++) {
        msg_nybbles[i] = 0;
    }

    // Move nybbles into a byte array
    // Compute parity and checksum at the same time.
    for (i = 0; i < LACROSSE_WS_BITLEN; i++) {
        rbyte_no = i / 8;
        rbit_no = 7 - (i % 8);
        mnybble_no = i / 4;
        mbit_no = 3 - (i % 4);
        bit = (pRow[rbyte_no] & (1 << rbit_no)) ? 1 : 0;
        msg_nybbles[mnybble_no] |= (bit << mbit_no);
        if (i == 9 || (i >= 27 && i <= 39))
            parity += bit;
    }

    for (i = 0; i < 12; i++) {
        checksum += msg_nybbles[i];
    }
    checksum = checksum & 0x0F;

    int checksum_ok = msg_nybbles[7] == (msg_nybbles[10] ^ 0xF)
            && msg_nybbles[8] == (msg_nybbles[11] ^ 0xF)
            && (parity & 0x1) == 0x1
            && checksum == msg_nybbles[12];

    if (!checksum_ok) {
        decoder_logf_bitrow(decoder, 2, __func__, msg_nybbles, LACROSSE_WS_BITLEN,
                "LaCrosse Packet Validation Failed error: Checksum Comp. %d != Recv. %d, Parity %d",
                checksum, msg_nybbles[12], parity);
        return DECODE_FAIL_MIC;
    }

    return 1;
}

static int lacrossews_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row;
    int events = 0;
    uint8_t msg_nybbles[(LACROSSE_WS_BITLEN / 4)];
    uint8_t ws_id, msg_type, sensor_id;
    // uint8_t msg_data, msg_unknown, msg_checksum;
    int msg_value_bcd, msg_value_bcd2, msg_value_bin;
    float temp_c, wind_dir, wind_spd, rain_mm;
    data_t *data;

    for (row = 0; row < bitbuffer->num_rows; row++) {
        // break out the message nybbles into separate bytes
        if (lacrossews_detect(decoder, bitbuffer->bb[row], msg_nybbles, bitbuffer->bits_per_row[row]) <= 0)
            continue; // DECODE_ABORT_EARLY

        ws_id          = (msg_nybbles[0] << 4) + msg_nybbles[1];
        msg_type       = ((msg_nybbles[2] >> 1) & 0x4) + (msg_nybbles[2] & 0x3);
        sensor_id      = (msg_nybbles[3] << 4) + msg_nybbles[4];
        //msg_data       = (msg_nybbles[5] << 1) + (msg_nybbles[6] >> 3);
        //msg_unknown    = msg_nybbles[6] & 0x01;
        msg_value_bcd  = msg_nybbles[7] * 100 + msg_nybbles[8] * 10 + msg_nybbles[9];
        msg_value_bcd2 = msg_nybbles[7] * 10 + msg_nybbles[8];
        msg_value_bin  = (msg_nybbles[7] * 256 + msg_nybbles[8] * 16 + msg_nybbles[9]);
        //msg_checksum   = msg_nybbles[12];

        switch (msg_type) {

        case 0: // Temperature
            if (ws_id == 0x6)
                temp_c = (msg_value_bcd - 400) * 0.1f;
            else
                temp_c = (msg_value_bcd - 300) * 0.1f;

            /* clang-format off */
            data = data_make(
                    "model",            "",             DATA_STRING, ws_id == 0x6 ? "LaCrosse-WS3600" : "LaCrosse-WS2310",
                    "id",               "",             DATA_INT,    sensor_id,
                    "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            events++;
            break;

        case 1: // Humidity
            if (msg_nybbles[7] == 0xA && msg_nybbles[8] == 0xA) {
                decoder_logf(decoder, 1, __func__, "LaCrosse WS %02X-%02X: Humidity Error",
                            ws_id, sensor_id);
                break;
            }

            /* clang-format off */
            data = data_make(
                    "model",            "",             DATA_STRING, ws_id == 0x6 ? "LaCrosse-WS3600" : "LaCrosse-WS2310",
                    "id",               "",             DATA_INT,    sensor_id,
                    "humidity",         "Humidity",     DATA_INT,    msg_value_bcd2,
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            events++;
            break;

        case 2: // Rain
            rain_mm = 0.5180f * msg_value_bin;

            /* clang-format off */
            data = data_make(
                    "model",            "",             DATA_STRING, ws_id == 0x6 ? "LaCrosse-WS3600" : "LaCrosse-WS2310",
                    "id",               "",             DATA_INT,    sensor_id,
                    "rain_mm",          "Rainfall",     DATA_FORMAT, "%3.2f mm", DATA_DOUBLE, rain_mm,
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            events++;
            break;

        case 3: // Wind

        case 7: // Gust
            wind_dir = msg_nybbles[9] * 22.5f;
            wind_spd = (msg_nybbles[7] * 16 + msg_nybbles[8]) * 0.1f;
            if (msg_nybbles[7] == 0xF && msg_nybbles[8] == 0xE) {
                decoder_logf(decoder, 1, __func__, "WS %02X-%02X: %s Not Connected",
                        ws_id, sensor_id, msg_type == 3 ? "Wind" : "Gust");
                break;
            }

            /* clang-format off */
            data = data_make(
                    "model",            "",             DATA_STRING, ws_id == 0x6 ? "LaCrosse-WS3600" : "LaCrosse-WS2310",
                    "id",               "",             DATA_INT,    sensor_id,
                    "wind_avg_m_s",     "Wind speed",   DATA_COND,   msg_type == 3, DATA_FORMAT, "%3.1f m/s", DATA_DOUBLE, wind_spd,
                    "wind_max_m_s",     "Gust speed",   DATA_COND,   msg_type != 3, DATA_FORMAT, "%3.1f m/s", DATA_DOUBLE, wind_spd,
                    "wind_dir_deg",     "Direction",    DATA_DOUBLE, wind_dir, NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            events++;
            break;

        default:
            decoder_logf(decoder, 1, __func__,
                    "WS %02X-%02X: Unknown data type %d, bcd %d bin %d\n",
                    ws_id, sensor_id, msg_type, msg_value_bcd, msg_value_bin);
            events++;
        }
    }

    return events;
}

static char *output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "humidity",
        "rain_mm",
        "wind_avg_m_s",
        "wind_max_m_s",
        "wind_dir_deg",
        NULL,
};

r_device lacrossews = {
        .name        = "LaCrosse WS-2310 / WS-3600 Weather Station",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 368,
        .long_width  = 1464,
        .reset_limit = 8000,
        .decode_fn   = &lacrossews_callback,
        .fields      = output_fields,
};
