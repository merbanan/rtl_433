/** @file
    Bresser SmartHome Garden set.

    Copyright (C) 2024 Bruno OCTAU (\@ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Bresser SmartHome Garden set.

- 7510100 : Weather Station Gateway Wifi RF 433.92 MHz (in 7510100/7510200 Sets)
- 7910100 : 1 zone water timer (1x) (in 7510100 Set)
- 7910101 : 2-zone water timer (1x) (in 7510200 Set)
- 7910102 : Soil moisture sensor (1x) (in 7510100/7510200 Sets), https://fccid.io/2AWDBTCS005FRF

Original brand is "Fujian Baldr Technology", see FCCID link above.

Homgar Family by Baldr:

- HWS388 : Weather Station Gateway https://fccid.io/2AWDBHWS388WRF
- HCS005 : Soil moisture sensor
- HTV103 : 1 zone water timer
- HTV203 : 2-zone water timer
- H666TH(outdoor) /H999TH (indoor with LCD): Thermo-hygro sensor.
- H0386 : External display timer.

RAINPOINT SMART IRRIGATION

- SOIL MOISTURE SENSOR ICS0001W
- 1 Zone WATER CONTROLLER SYSTEM ITV0103W/TTV1013WRF
- 2 Zone WATER CONTROLLER SYSTEM TTV203WRF
- TWG004WRF Wifi Hub/Sockect with power (Wifi RF 433 Wateway)

Issue #2988 open by \@kami83 to add support for Bresser Soil Moisture Sensor.
Product web page : https://www.bresser.de/en/Weather-Time/BRESSER-Soil-Sensor-for-7510100-7510200-Smart-Garden-Smart-Home-Irrigation-System.html

The protocol is :

- Bidirectional : The messages are sent from the source to the target, then the target acknowledges receipt of the message to the source.
- The Soil Moisture Sensor communicates with Weather Station Gateway and with Water Timer Valve.
- Each device has a unique identifier that does not change after battery replacement.
- Depending on the type of message, the information is coded differently, but the global message length is always 33 bytes (after preamble/syncword).

Flex decoder:

    rtl_433 -R 0 -X "n=Bresser_FSK,m=FSK_PCM,s=50,l=50,r=10000,bits>=40,bits<=1000,preamble=aaf3" -M level -Y minmax -Y magest -s 2048k 2>&1 | grep codes

    codes     : {298}e9105e51000000001f05004701010805ff4747000435030000000000000000000000007ab60
    codes     : {298}e9105e511f05004788160001018110000505e001b946ed110102000000000000000000ec640
    codes     : {298}e9105e51881600011f050047020307050988008527030000000000000000000000000067220
    codes     : {298}e9105e511f050047881600010283010000000000000000000000000000000000000000dcc90

Data layout:

    Byte Position                   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33
               preample syncword   TT TT TT TT SS SS SS SS RR AC LL MM MM MM MM MM MM MM MM MM MM MM MM MM MM MM MM MM MM MM MM ZZ ZZ XX
                                                                    ID ?? ?? ?? ?? ?? FF ??
    Sensor INIT aaaaaaa f3e9105e51 00 00 00 00 1f 05 00 47 01 01 08 05 ff 47 47 00 04 35 03 00 00 00 00 00 00 00 00 00 00 00 00 7a b6 0
                                                                    ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ??
    Base acknowledgemt    e9105e51 1f 05 00 47 88 16 00 01 01 81 10 00 05 05 e0 01 b9 46 ed 11 01 02 00 00 00 00 00 00 00 00 00 ec 64 0
                                                                    ID BB 88 HH 85 TEMP
    Sensor Send T/H       e9105e51 88 16 00 01 1f 05 00 47 02 03 07 05 09 88 00 85 27 03 00 00 00 00 00 00 00 00 00 00 00 00 00 67 22 0

    Base acknowledge T/H  e9105e51 1f 05 00 47 88 16 00 01 02 83 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 dc c9 0

Global Message data layout:

- TT:{32} Target ID, little indian, during init/paring the target id is 0x00000000
- SS:{32} Sensor ID, little indian, hard coded into the sensor.
- RR: {8} Counter, value increments with each message, except for acknowledgement the value is repeated.
-  A: {4} 0x0 for send, 0x8 for acknowledgement.
-  C: {4} 0x1 for init, 0x3 for normal transmit.
- LL: {8} Sub message length
- MM:{160}Sub message
- ZZ:{16} CRC-16, poly 0x1021, init 0xd636
- XX: Trailing bit

Sub Message SEND/INIT: (0x01)

- ID:{8} Device type ID, 0x05 for Soil Moisture Sensor
- ??: Unknown
- FF:{8} Looks like firmware 0x35 = 53.

Sub Message Acknowledgement/INIT: (0x81)

- ??: Unknown, not yet idenfy

Sub Message SEND Temp Hum: (0x03)

- ID:{8} Device type ID, 0x05 for Soil Moisture Sensor
- BB:{8} Battery information, 0x09 = Full battery, 0x11 = Low Battery.
         Last nibble probably the battery level, 1 for 3.6 / 3.8V , 9 for 4.5 V
         First nibble probably the low battery flag.
- 88:{8} Fixed value 0x88, not yet identify
- HH:{8} Humidity / Moisture %
- 85:{8} Fixed value 0x85, not yet identify
- TEMP:{16} Temperature_F, little indian, scale 10.

Sub Message Acknowlegement/Temp Hum: (0x83)

- sub Message is always empty with zeros.

*/

#include "decoder.h"

static int bresser_garden_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = { 0xaa, 0xf3, 0xe9, 0x10, 0x5e, 0x51};

    uint8_t b[33];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    int msg_len = bitbuffer->bits_per_row[0];

    if (msg_len > 630) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    int offset = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= msg_len) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    if ((msg_len - offset ) < 264 ) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 33 * 8);

    if (crc16(b,33,0x1021,0xd636)) {
        decoder_logf(decoder, 1, __func__, "CRC error");
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 1, __func__, b, 33 * 8 , "MSG");

    //Extract info ...

    uint32_t target_id = (b[3] << 24) | (b[2] << 16) | (b[1] << 8) | b[0];
    uint32_t source_id = (b[7] << 24) | (b[6] << 16) | (b[5] << 8) | b[4];
    int counter        = b[8];
    int msg_type       = b[9];
    int msg_length     = b[10];
    int acknowledgement = (msg_type & 0xf0) >> 7;

    // if Soil Moisture Sensor message ?
    if (msg_type == 0x03 && msg_length == 0x07) {

        int sensor_number = b[11];
        int battery_low   = (b[12] & 0x10) >> 4;
        int battery_level = (b[12] & 0x0f);
        int flag1         = b[13];
        int moisture      = b[14];
        int flag2         = b[15];
        int temperature_f = (b[17] << 8) | b[16];

        /* clang-format off */
        data_t *data = data_make(
                "model",         "",              DATA_STRING, "Bresser-SoilMoisture",
                "id",            "",              DATA_FORMAT, "%u",   DATA_INT,    source_id,
                "sensor_number", "",              DATA_FORMAT, "%u",   DATA_INT,    sensor_number,
                "station_id",    "",              DATA_FORMAT, "%u",   DATA_INT,    target_id,
                "msg_counter",   "Msg Counter",   DATA_INT,    counter,
                "temperature_F", "Temperature",   DATA_FORMAT, "%.1f F", DATA_DOUBLE, temperature_f * 0.1f,
                "moisture",      "Moisture",      DATA_FORMAT, "%u %%",DATA_INT,    moisture,
                "flag1",         "Flag1",         DATA_FORMAT, "%01x", DATA_INT,    flag1,
                "flag2",         "Flag2",         DATA_FORMAT, "%01x", DATA_INT,    flag2,
                "battery_ok",    "Battery OK",    DATA_FORMAT, "%u",   DATA_INT,    !battery_low,
                "battery_level", "Battery Level", DATA_INT, battery_level,
                "mic",           "Integrity",     DATA_STRING,    "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    // if Soil Moisture Init message ?
    else if (msg_type == 0x01 && msg_length == 0x08) {

        int sensor_number = b[11];
        int firmware      = b[17];

        /* clang-format off */
        data_t *data = data_make(
                "model",         "",            DATA_STRING, "Bresser-Garden",
                "status",        "",            DATA_STRING, "Init Pairing",
                "id",            "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "sensor_number", "",            DATA_FORMAT, "%u", DATA_INT, sensor_number,
                "firmware",      "Firmware",    DATA_FORMAT, "%u", DATA_INT, firmware,
                "mic",           "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    else if (msg_type == 0x81 && msg_length == 0x10) {

        // Acknowledgement but message answer not yet decoded, not always same values, could be date and time information ?
        // 11 12 13 14 15 16 17 18 19 20 21
        //
        // 00 05 05 e0 01 5a 9a e8 11 06 02
        // 00 05 05 e0 01 b9 46 ed 11 01 02
        // 00 05 05 e0 01 2d 48 ed 11 01 02
        // 00 05 05 e0 01 6c 48 ed 11 01 02
        // 00 05 05 e0 01 3b 4c ed 11 01 02

        char msg[23];
        snprintf(msg, 23, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                 b[11],b[12],b[13],b[14],b[15],b[16],b[17],b[18],b[19],b[20],b[21]);

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",            DATA_STRING, "Bresser-Garden",
                "status",          "",            DATA_STRING, "Pairing Acknowledgement",
                "id",              "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",       "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",     "Msg Counter", DATA_INT,    counter,
                "acknowledgement", "",            DATA_INT,    acknowledgement,
                "msg_type",        "",            DATA_FORMAT, "%0X", DATA_INT, msg_type & 0xf,
                "msg_length",      "",            DATA_FORMAT, "%02X", DATA_INT, msg_length,
                "msg",             "",            DATA_STRING, msg,
                "mic",             "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    else if (msg_type == 0x83 && msg_length == 0x01) {

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",            DATA_STRING, "Bresser-Garden",
                "status",          "",            DATA_STRING, "Pairing Acknowledgement",
                "id",              "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",       "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",     "Msg Counter", DATA_INT,    counter,
                "acknowledgement", "",            DATA_INT,    acknowledgement,
                "msg_type",        "",            DATA_FORMAT, "%0X", DATA_INT, msg_type & 0xf,
                "msg_length",      "",            DATA_FORMAT, "%02X", DATA_INT, msg_length,
                "mic",             "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    else {

        // Not yet decoded the Water Timer actuator
        //decoder_log_bitrow(decoder, 0, __func__, &b[11], msg_length * 8 , "Unknown MSG");
        char msg[41];
        snprintf(msg, 41, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                 b[11],b[12],b[13],b[14],b[15],b[16],b[17],b[18],b[19],b[20],b[21],b[22],b[23],b[24],b[25],b[26],b[27],b[28],b[29],b[30]);

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",            DATA_STRING, "Bresser-Garden",
                "status",          "",            DATA_STRING, "Unknown msg",
                "id",              "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",       "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",     "Msg Counter", DATA_INT,    counter,
                "acknowledgement", "",            DATA_INT,    acknowledgement,
                "msg_type",        "",            DATA_FORMAT, "%0X", DATA_INT, msg_type & 0xf,
                "msg_length",      "",            DATA_FORMAT, "%02X", DATA_INT, msg_length,
                "msg",             "",            DATA_STRING, msg,
                "mic",             "Integrity",   DATA_STRING, "CRC",
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
        "serial_id",
        "temperature_F",
        "status",
        "firmware",
        "moisture",
        "humidity",
        "flag1",
        "flag2",
        "battery_ok",
        "battery_level",
        "msg_type",
        "msg_length",
        "msg",
        "mic",
        NULL,
};

r_device const bresser_garden = {
        .name        = "Bresser SmartHome Garden set 7510100/7510200 with Soil Moisture Sensor 7910102, Baldr Homgar Family, RainPoint Smart Irrigation",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 50,
        .long_width  = 50,
        .reset_limit = 10000, // long part of the message could be zeros
        .decode_fn   = &bresser_garden_decode,
        .fields      = output_fields,
};
