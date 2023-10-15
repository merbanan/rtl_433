/** @file
    ERT Interval Data Message (IDM) and Interval Data Message (IDM) for Net Meters.

    Copyright (C) 2020 Peter Shipley <peter.shipley@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/*
Freq 912600155

Random information:

This file contains supports callbacks for both IDM and NetIDM Given the similarities.

Currently the code is unable to differentiate between the the two
similar protocols thus both will respond to the same packet. As
of this time I am unable to find any documentation on how to
differentiate IDM and NetIDM packets as both use identical use Sync
ID / Packet Type / length / App Version ID and CRC.

Eventually ert_idm_decode() and ert_netidm_decode() may be merged.

https://github.com/bemasher/rtlamr/wiki/Protocol
http://www.gridinsight.com/community/documentation/itron-ert-technology/
*/

#define IDM_PACKET_BYTES 92
#define IDM_PACKET_BITLEN 720
// 92 * 8

// Least significant nibble of endpoint_type is equivalent to SCM's endpoint type field
// id info from https://github.com/bemasher/rtlamr/wiki/Compatible-Meters
static char const *get_meter_type_name(uint8_t ERTType)
{
    switch (ERTType & 0x0f) {
    case 4:
    case 5:
    case 7:
    case 8:
        return "Electric";
    case 0:
    case 1:
    case 2:
    case 9:
    case 12:
        return "Gas";
    case 3:
    case 11:
    case 13:
        return "Water";
    default:
        return "unknown";
    }
}

/**
ERT Interval Data Message (IDM).

IDM layout:

Field                 | Length | Offset/byte index
--- | --- | ---
pream                 | 2      |
Sync Word             | 2      | 0
Packet Type           | 1      | 2
Packet Length         | 1      | 3
Hamming Code          | 1      | 4
Application Version   | 1      | 5
Endpoint Type         | 1      | 6
Endpoint ID           | 4      | 7
Consumption Interval  | 1      | 11
Mod Programming State | 1      | 12
Tamper Count          | 6      | 13
Async Count           | 2      | 19
Power Outage Flags    | 6      | 21
Last Consumption      | 4      | 27
Diff Consumption      | 53     | 31
Transmit Time Offset  | 2      | 84
Meter ID Checksum     | 2      | 86
Packet Checksum       | 2      | 88
*/
static int ert_idm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t b[IDM_PACKET_BYTES];
    data_t *data;
    unsigned sync_index;
    const uint8_t idm_frame_sync[] = {0x16, 0xA3, 0x1C};

    uint8_t PacketTypeID;
    char PacketTypeID_str[5];
    uint8_t PacketLength;
    // char    PacketLength_str[5];
    //uint8_t HammingCode;
    // char    HammingCode_str[5];
    uint8_t ApplicationVersion;
    // char    ApplicationVersion_str[5];
    uint8_t ERTType;
    // char    ERTType_str[5];
    uint32_t ERTSerialNumber;
    uint8_t ConsumptionIntervalCount;
    uint8_t ModuleProgrammingState;
    // char  ModuleProgrammingState_str[5];
    // uint64_t TamperCounters = 0;  // 6 bytes
    char TamperCounters_str[16];
    uint16_t AsynchronousCounters;
    // char AsynchronousCounters_str[8];
    //uint64_t PowerOutageFlags = 0; // 6 bytes
    char PowerOutageFlags_str[16];
    uint32_t LastConsumptionCount;
    uint32_t DifferentialConsumptionIntervals[47] = {0}; // 47 intervals of 9-bit unsigned integers
    uint16_t TransmitTimeOffset;
    uint16_t MeterIdCRC;
    // char  MeterIdCRC_str[8];
    uint16_t PacketCRC;
    // char  PacketCRC_str[8];

    if (bitbuffer->bits_per_row[0] > 600) {
        decoder_logf(decoder, 1, __func__, "rows=%hu, row0 len=%hu", bitbuffer->num_rows, bitbuffer->bits_per_row[0]);
    }

    if (bitbuffer->bits_per_row[0] < IDM_PACKET_BITLEN) {
        return (DECODE_ABORT_LENGTH);
    }

    sync_index = bitbuffer_search(bitbuffer, 0, 0, idm_frame_sync, 24);

    decoder_logf(decoder, 1, __func__, "sync_index=%u", sync_index);

    if (sync_index >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }

    if ((bitbuffer->bits_per_row[0] - sync_index) < IDM_PACKET_BITLEN) {
        return DECODE_ABORT_LENGTH;
    }

    // bitbuffer_debug(bitbuffer);
    bitbuffer_extract_bytes(bitbuffer, 0, sync_index, b, IDM_PACKET_BITLEN);
    decoder_log_bitrow(decoder, 1, __func__, b, IDM_PACKET_BITLEN, "");

    // uint32_t t_16; // temp vars
    // uint32_t t_32;
    // uint64_t t_64;
    char *p;

    uint16_t crc;
    // memcpy(&t_16, &b[88], 2);
    // pkt_checksum = ntohs(t_16);
    // pkt_checksum = (b[88] << 8 | b[89]);
    PacketCRC = (b[88] << 8 | b[89]);

    crc = crc16(&b[2], 86, 0x1021, 0xD895);
    if (crc != PacketCRC) {
        return DECODE_FAIL_MIC;
    }

    // snprintf(XX_str, sizeof(XX_str), "0x%02X", XX);

    PacketTypeID = b[2];
    snprintf(PacketTypeID_str, sizeof(PacketTypeID_str), "0x%02X", PacketTypeID);

    PacketLength = b[3];
    // snprintf(PacketLength_str, sizeof(PacketLength_str), "0x%02X", PacketLength);

    //HammingCode = b[4];
    // snprintf(HammingCode_str, sizeof(HammingCode_str), "0x%02X", HammingCode);

    ApplicationVersion = b[5];
    // snprintf(ApplicationVersion_str, sizeof(ApplicationVersion_str), "0x%02X", ApplicationVersion);

    ERTType = b[6]; // & 0x0F;
    // snprintf(ERTType_str, sizeof(ERTType_str), "0x%02X", ERTType);

    // memcpy(&t_32, &b[7], 4);
    // ERTSerialNumber = ntohl(t_32);
    ERTSerialNumber = ((uint32_t)b[7] << 24) | (b[8] << 16) | (b[9] << 8) | (b[10]);

    ConsumptionIntervalCount = b[11];

    ModuleProgrammingState = b[12];
    // snprintf(ModuleProgrammingState_str, sizeof(ModuleProgrammingState_str), "0x%02X", ModuleProgrammingState);

    /*
    http://davestech.blogspot.com/2008/02/itron-remote-read-electric-meter.html
    SCM1 Counter1 Meter has been inverted
    SCM1 Counter2 Meter has been removed
    SCM2 Counter3 Meter detected a button–press demand reset
    SCM2 Counter4 Meter has a low-battery/end–of–calendar warning
    SCM3 Counter5 Meter has an error or a warning that can affect billing
    SCM3 Counter6 Meter has a warning that may or may not require a site visit,
    */
    p = TamperCounters_str;
    strncpy(p, "0x", sizeof(TamperCounters_str));
    p += 2;
    for (int j = 0; j < 6; j++) {
        p += sprintf(p, "%02X", b[13 + j]);
    }
    decoder_logf_bitrow(decoder, 2, __func__, &b[13], 6 * 8, "TamperCounters_str   %s", TamperCounters_str);

    AsynchronousCounters = (b[19] << 8 | b[20]);
    // snprintf(AsynchronousCounters_str, sizeof(AsynchronousCounters_str), "0x%04X", AsynchronousCounters);

    p = PowerOutageFlags_str;
    strncpy(p, "0x", sizeof(PowerOutageFlags_str));
    p += 2;
    for (int j = 0; j < 6; j++) {
        p += sprintf(p, "%02X", b[21 + j]);
    }
    decoder_logf_bitrow(decoder, 2, __func__, &b[21], 6 * 8, "PowerOutageFlags_str %s", PowerOutageFlags_str);

    LastConsumptionCount = ((uint32_t)b[27] << 24) | (b[28] << 16) | (b[29] << 8) | (b[30]);
    decoder_logf_bitrow(decoder, 1, __func__, &b[27], 32, "LastConsumptionCount %d", LastConsumptionCount);

    // DifferentialConsumptionIntervals : 47 intervals of 9-bit unsigned integers
    decoder_log_bitrow(decoder, 2, __func__, &b[31], 423, "DifferentialConsumptionIntervals");
    unsigned pos = sync_index + (31 * 8);
    for (int j = 0; j < 47; j++) {
        uint8_t buffy[4] = {0};

        bitbuffer_extract_bytes(bitbuffer, 0, pos, buffy, 9);
        DifferentialConsumptionIntervals[j] = ((uint16_t)buffy[0] << 1) | (buffy[1] >> 7);
        pos += 9;
    }
    if (decoder->verbose > 1) {
        decoder_log(decoder, 2, __func__, "DifferentialConsumptionIntervals");
        for (int j = 0; j < 47; j++) {
            decoder_logf(decoder, 2, __func__, "%d", DifferentialConsumptionIntervals[j]);
        }
    }

    TransmitTimeOffset = (b[84] << 8 | b[85]);

    MeterIdCRC = (b[86] << 8 | b[87]);
    // snprintf(SerialNumberCRC_str, sizeof(MeterIdCRC_str), "0x%04X", MeterIdCRC);

    //  snprintf(PacketCRC_str, sizeof(PacketCRC_str), "0x%04X", PacketCRC);

    // Least significant nibble of endpoint_type is  equivalent to SCM's endpoint type field
    // id info from https://github.com/bemasher/rtlamr/wiki/Compatible-Meters

    char const *meter_type = get_meter_type_name(ERTType);
    // decoder_logf(decoder, 0, __func__, "meter_type = %s", meter_type);

    /*
        Field key names and format set to  match rtlamr field names

        {"Time":"2020-06-25T08:22:52.404629556-04:00","Offset":1835008,"Length":229376,"Type":"IDM","Message":
        {"Preamble":1431639715,"PacketTypeID":28,"PacketLength":92,"HammingCode":198,"ApplicationVersion":4,"ERTType":7,
         "ERTSerialNumber":11278109,"ConsumptionIntervalCount":246,"ModuleProgrammingState":188,
         "TamperCounters":"QgUWry0H","AsynchronousCounters":0,"PowerOutageFlags":"QUgmCEEF","LastConsumptionCount":339972,
         "DifferentialConsumptionIntervals":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0],
         "TransmitTimeOffset":476,"SerialNumberCRC":60090,"PacketCRC":31799}}
    */

    /* clang-format off */
    data = data_make(
            "model",                            "",    DATA_STRING, "IDM",

            // "PacketTypeID",             "",             DATA_FORMAT, "0x%02X", DATA_INT, PacketTypeID,
            "PacketTypeID",                     "",    DATA_STRING,       PacketTypeID_str,
            "PacketLength",                     "",    DATA_INT,       PacketLength,
            // "HammingCode",              "",             DATA_INT,          HammingCode,
            "ApplicationVersion",               "",     DATA_INT,       ApplicationVersion,
            "ERTType",                          "",     DATA_FORMAT,  "0x%02X", DATA_INT,    ERTType,
            // "ERTType",                          "",     DATA_INT,       ERTType,
            "ERTSerialNumber",                  "",     DATA_INT,       ERTSerialNumber,
            "ConsumptionIntervalCount",         "",     DATA_INT,       ConsumptionIntervalCount,
            // "ModuleProgrammingState",           "",     DATA_FORMAT, "0x%02X", DATA_INT, ModuleProgrammingState,
            "ModuleProgrammingState",           "",     DATA_FORMAT, "0x%02X", DATA_INT, ModuleProgrammingState,
            // "ModuleProgrammingState",           "",     DATA_INT,      ModuleProgrammingState,
            "TamperCounters",                   "",     DATA_STRING,       TamperCounters_str,
            "AsynchronousCounters",             "",     DATA_FORMAT, "0x%02X", DATA_INT, AsynchronousCounters,
            // "AsynchronousCounters",             "",     DATA_INT,    AsynchronousCounters,

            "PowerOutageFlags",                 "",     DATA_STRING,       PowerOutageFlags_str ,
            "LastConsumptionCount",             "",     DATA_INT,       LastConsumptionCount,
            "DifferentialConsumptionIntervals", "",     DATA_ARRAY, data_array(47, DATA_INT, DifferentialConsumptionIntervals),
            "TransmitTimeOffset",               "",     DATA_INT,       TransmitTimeOffset,
            "MeterIdCRC",                       "",     DATA_FORMAT, "0x%04X", DATA_INT, MeterIdCRC,
            "PacketCRC",                        "",     DATA_FORMAT, "0x%04X", DATA_INT, PacketCRC,

            "MeterType",                        "Meter_Type",       DATA_STRING, meter_type,
            "mic",                              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
Interval Data Message (IDM) for Net Meters.

NetIDM layout:

Field                 | Length | Offset/byte index
--- | --- | ---
Preamble              | 2
Sync Word             | 2      | 0
Protocol ID           | 1      | 2
Packet Length         | 1      | 3
Hamming Code          | 1      | 4
Application Version   | 1      | 5
Endpoint Type         | 1      | 6
Endpoint ID           | 4      | 7
Consumption Interval  | 1      | 11
Programming State     | 1      | 12
Tamper Count          | 6      | 13  - New
Unknown_1             | 7      | 19  - New
Unknown_1             | 13     | 13  - Old
Last Generation Count | 3      | 26
Unknown_2             | 3      | 29
Last Consumption Count| 4      | 32
Differential Cons     | 48     | 36    27 intervals of 14-bit unsigned integers.
Transmit Time Offset  | 2      | 84
Meter ID Checksum     | 2      | 86    CRC-16-CCITT of Meter ID.
Packet Checksum       | 2      | 88    CRC-16-CCITT of packet starting at Packet Type.
*/
static int ert_netidm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t b[IDM_PACKET_BYTES];
    data_t *data;
    unsigned sync_index;
    const uint8_t idm_frame_sync[] = {0x16, 0xA3, 0x1C};

    uint8_t PacketTypeID;
    char PacketTypeID_str[5];
    uint8_t PacketLength;
    // char    PacketLength_str[5];
    //uint8_t HammingCode;
    // char    HammingCode_str[5];
    uint8_t ApplicationVersion;
    // char    ApplicationVersion_str[5];
    uint8_t ERTType;
    // char    ERTType_str[5];
    uint32_t ERTSerialNumber;
    uint8_t ConsumptionIntervalCount;
    uint8_t ModuleProgrammingState;
    // char  ModuleProgrammingState_str[5];

    //uint8_t Unknown_field_1[13];
    char Unknown_field_1_str[32];

    uint32_t LastGenerationCount = 0;
    //char LastGenerationCount_str[16];

    //uint8_t Unknown_field_2[3];
    char Unknown_field_2_str[9];

    uint32_t LastConsumptionCount;
    //char LastConsumptionCount_str[16];

    // uint64_t TamperCounters = 0;  // 6 bytes
    char TamperCounters_str[16];
    // uint16_t AsynchronousCounters;
    // char AsynchronousCounters_str[8];
    // uint64_t PowerOutageFlags = 0;  // 6 bytes
    // char  PowerOutageFlags_str[16];

    uint32_t DifferentialConsumptionIntervals[27] = {0}; // 27 intervals of 14-bit unsigned integers

    uint16_t TransmitTimeOffset;
    uint16_t MeterIdCRC;
    // char  MeterIdCRC_str[8];
    uint16_t PacketCRC;
    // char  PacketCRC_str[8];

    if (bitbuffer->bits_per_row[0] > 600) {
        decoder_logf(decoder, 1, __func__, "rows=%d, row0 len=%hu", bitbuffer->num_rows, bitbuffer->bits_per_row[0]);
    }

    if (bitbuffer->bits_per_row[0] < IDM_PACKET_BITLEN) {
        return (DECODE_ABORT_LENGTH);
    }

    sync_index = bitbuffer_search(bitbuffer, 0, 0, idm_frame_sync, 24);

    decoder_logf(decoder, 1, __func__, "sync_index=%u", sync_index);

    if (sync_index >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }

    if ((bitbuffer->bits_per_row[0] - sync_index) < IDM_PACKET_BITLEN) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, sync_index, b, IDM_PACKET_BITLEN);
    decoder_log_bitrow(decoder, 1, __func__, b, IDM_PACKET_BITLEN, "");

    // uint32_t t_16; // temp vars
    // uint32_t t_32;
    // uint64_t t_64;
    char *p;

    uint16_t crc;
    // memcpy(&t_16, &b[88], 2);
    // pkt_checksum = ntohs(t_16);
    // pkt_checksum = (b[88] << 8 | b[89]);
    PacketCRC = (b[88] << 8 | b[89]);

    crc = crc16(&b[2], 86, 0x1021, 0xD895);
    if (crc != PacketCRC) {
        return DECODE_FAIL_MIC;
    }

    // snprintf(PacketCRC_str, sizeof(PacketCRC_str), "0x%04X", PacketCRC);

    PacketTypeID = b[2];
    snprintf(PacketTypeID_str, sizeof(PacketTypeID_str), "0x%02X", PacketTypeID);

    PacketLength = b[3];
    // snprintf(PacketLength_str, sizeof(PacketLength_str), "0x%02X", PacketLength);

    //HammingCode = b[4];
    // snprintf(HammingCode_str, sizeof(HammingCode_str), "0x%02X", HammingCode);

    ApplicationVersion = b[5];
    // snprintf(ApplicationVersion_str, sizeof(ApplicationVersion_str), "0x%02X", b[5]);

    ERTType = b[6]; // & 0x0f;
    // snprintf(ERTType_str, sizeof(ERTType_str), "0x%02X", ERTType);

    // memcpy(&t_32, &b[7], 4);
    // ERTSerialNumber = ntohl(t_32);
    ERTSerialNumber = ((uint32_t)b[7] << 24) | (b[8] << 16) | (b[9] << 8) | (b[10]);

    ConsumptionIntervalCount = b[11];

    ModuleProgrammingState = b[12];
    // snprintf(ModuleProgrammingState_str, sizeof(ModuleProgrammingState_str), "0x%02X", ModuleProgrammingState);

    /*
    http://davestech.blogspot.com/2008/02/itron-remote-read-electric-meter.html
    SCM1 Counter1 Meter has been inverted
    SCM1 Counter2 Meter has been removed
    SCM2 Counter3 Meter detected a button–press demand reset
    SCM2 Counter4 Meter has a low-battery/end–of–calendar warning
    SCM3 Counter5 Meter has an error or a warning that can affect billing
    SCM3 Counter6 Meter has a warning that may or may not require a site visit,
    */
    p = TamperCounters_str;
    strncpy(p, "0x", sizeof(TamperCounters_str));
    p += 2;
    for (int j = 0; j < 6; j++) {
        p += sprintf(p, "%02X", b[13 + j]);
    }
    decoder_logf_bitrow(decoder, 2, __func__, &b[13], 6 * 8, "TamperCounters_str   %s", TamperCounters_str);

    //  should this be included ?
    p = Unknown_field_1_str;
    strncpy(p, "0x", sizeof(Unknown_field_1_str));
    p += 2;
    for (int j = 0; j < 7; j++) {
        p += sprintf(p, "%02X", b[19 + j]);
    }
    decoder_logf_bitrow(decoder, 1, __func__, &b[19], 7 * 8, "Unknown_field_1 %s", Unknown_field_1_str);

    // 3 bytes
    LastGenerationCount = ((uint32_t)(b[26] << 16)) | (b[27] << 8) | (b[28]);

    //  should this be included ?
    p = Unknown_field_2_str;
    strncpy(p, "0x", sizeof(Unknown_field_2_str));
    p += 2;
    for (int j = 0; j < 3; j++) {
        p += sprintf(p, "%02X", b[29 + j]);
    }
    decoder_logf_bitrow(decoder, 1, __func__, &b[29], 3 * 8, "Unknown_field_1 %s", Unknown_field_2_str);

    LastConsumptionCount = ((uint32_t)b[32] << 24) | (b[33] << 16) | (b[34] << 8) | (b[35]);

    decoder_logf_bitrow(decoder, 1, __func__, &b[32], 32, "LastConsumptionCount %d", LastConsumptionCount);

    // DifferentialConsumptionIntervals[] = 27 intervals of 14-bit unsigned integers.
    unsigned pos = sync_index + (36 * 8);
    decoder_log_bitrow(decoder, 1, __func__, &b[36], 48 * 8, "DifferentialConsumptionIntervals");
    for (int j = 0; j < 27; j++) {
        uint8_t buffy[4] = {0};

        bitbuffer_extract_bytes(bitbuffer, 0, pos, buffy, 14);
        DifferentialConsumptionIntervals[j] = ((uint16_t)buffy[0] << 6) | (buffy[1] >> 2);
        // decoder_logf_bitrow(decoder, 0, __func__, buffy, 14, "%d %d", j, DifferentialConsumptionIntervals[j]);
        pos += 14;
    }
    if (decoder->verbose) {
        decoder_log(decoder, 1, __func__, "DifferentialConsumptionIntervals");
        for (int j = 0; j < 27; j++) {
            decoder_logf(decoder, 1, __func__, "%d", DifferentialConsumptionIntervals[j]);
        }
    }

    TransmitTimeOffset = (b[84] << 8 | b[85]);

    MeterIdCRC = (b[86] << 8 | b[87]);
    // snprintf(MeterIdCRC_str, sizeof(MeterIdCRC_str), "0x%04X", MeterIdCRC);

    // Least significant nibble of endpoint_type is  equivalent to SCM's endpoint type field
    // id info from https://github.com/bemasher/rtlamr/wiki/Compatible-Meters
    /*
    char *meter_type =  get_meter_type_name(ERTType);
    switch (ERTType & 0x0f) {
    case 4:
    case 5:
    case 7:
    case 8:
        meter_type = "Electric";
        break;
    case 2:
    case 9:
    case 12:
        meter_type = "Gas";
        break;
    case 11:
    case 13:
        meter_type = "Water";
        break;
    default:
        meter_type = "unknown";
        break;
    }
    */

    char const *meter_type = get_meter_type_name(ERTType);

    // decoder_logf(decoder, 0, __func__, "meter_type = %s", meter_type);

    /*
        Field key names and format set to  match rtlamr field names

        {Time":"2020-06-25T08:22:08.569276915-04:00","Offset":1605632,"Length":229376,"Type":"NetIDM","Message":
        {"Preamble":1431639715,"ProtocolID":28,"PacketLength":92,"HammingCode":198,"ApplicationVersion":4,"ERTType":7,
         "ERTSerialNumber":1550406067,"ConsumptionIntervalCount":30,"ProgrammingState":184,"LastGeneration":125,
         "LastConsumption":0,"LastConsumptionNet":2223120656,"DifferentialConsumptionIntervals":
          [7695,545,2086,1475,6240,2180,4240,4616,240,7191,609,7224,1603,96,2052,12464,6152,8480,9226,352,12312,833,10292,1795,4248,4613,8416],
         "TransmitTimeOffset":2145,"SerialNumberCRC":61178,"PacketCRC":37271}}

    */

    /* clang-format off */
    data = data_make(
            "model",                            "",     DATA_STRING, "NETIDM",

            "PacketTypeID",                     "",     DATA_STRING,       PacketTypeID_str,
            "PacketLength",                     "",     DATA_INT,       PacketLength,
            // "HammingCode",              "",             DATA_FORMAT, "0x%02X", DATA_INT, HammingCode,
            "ApplicationVersion",               "",     DATA_INT,       ApplicationVersion,

            "ERTType",                          "",     DATA_FORMAT,  "0x%02X", DATA_INT,    ERTType,
            "ERTSerialNumber",                  "",     DATA_INT,       ERTSerialNumber,
            "ConsumptionIntervalCount",         "",     DATA_INT,       ConsumptionIntervalCount,
            "ModuleProgrammingState",           "",     DATA_FORMAT, "0x%02X", DATA_INT, ModuleProgrammingState,
            // "ModuleProgrammingState",           "",     DATA_STRING,    ModuleProgrammingState_str,
            "TamperCounters",                   "",     DATA_STRING,       TamperCounters_str,
            // "AsynchronousCounters",             "",     DATA_FORMAT, "0x%02X", DATA_INT, AsynchronousCounters,
            "Unknown_field_1",                  "",     DATA_STRING,    Unknown_field_1_str,
            "LastGenerationCount",              "",     DATA_INT,       LastGenerationCount,
            "Unknown_field_2",                  "",     DATA_STRING,    Unknown_field_2_str,

            // "AsynchronousCounters",             "",     DATA_STRING,    AsynchronousCounters_str,

            // "PowerOutageFlags",                 "",     DATA_STRING,       PowerOutageFlags_str ,
            "LastConsumptionCount",             "",     DATA_INT,       LastConsumptionCount,
            "DifferentialConsumptionIntervals", "",     DATA_ARRAY, data_array(27, DATA_INT, DifferentialConsumptionIntervals),
            "TransmitTimeOffset",               "",     DATA_INT,       TransmitTimeOffset,
            "MeterIdCRC",                       "",     DATA_FORMAT, "0x%04X", DATA_INT, MeterIdCRC,
            "PacketCRC",                        "",     DATA_FORMAT, "0x%04X", DATA_INT, PacketCRC,

            "MeterType",                        "",       DATA_STRING, meter_type,
            "mic",                              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {

        // Common fields
        "model",
        "PacketTypeID",
        "PacketLength",
        "HammingCode",
        "ApplicationVersion",
        "ERTType",
        "ERTSerialNumber",
        "ConsumptionIntervalCount",
        "ModuleProgrammingState",

        // NetIDM Only
        "Unknown_field_1",
        "LastGenerationCount",
        "Unknown_field_2",

        // IDM Only
        "TamperCounters",
        "AsynchronousCounters",
        "PowerOutageFlags",

        // Common fields
        "LastConsumptionCount",
        "DifferentialConsumptionIntervals",
        "TransmitTimeOffset",
        "MeterIdCRC",
        "PacketCRC",
        "MeterType",
        "mic",
        NULL,
};

//      Freq 912600155
//     -X n=L58,m=OOK_MC_ZEROBIT,s=30,l=30,g=20000,r=20000,match={24}0x16a31e,preamble={1}0x00

r_device const ert_idm = {
        .name        = "ERT Interval Data Message (IDM)",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 30,
        .long_width  = 0, // not used
        .gap_limit   = 20000,
        .reset_limit = 20000,
        // .gap_limit   = 2500,
        // .reset_limit = 4000,
        .decode_fn = &ert_idm_decode,
        .fields    = output_fields,
};

r_device const ert_netidm = {
        .name        = "ERT Interval Data Message (IDM) for Net Meters",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 30,
        .long_width  = 0, // not used
        .gap_limit   = 20000,
        .reset_limit = 20000,
        // .gap_limit   = 2500,
        // .reset_limit = 4000,
        .decode_fn = &ert_netidm_decode,
        .fields    = output_fields,
};
