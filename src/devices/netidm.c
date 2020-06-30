/** @file
    ERT Interval Data Message (IDM) for Net Meters

    Copyright (C) 2020 Peter Shipley <peter.shipley@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**

Freq 912600155

Random information:

https://github.com/bemasher/rtlamr/wiki/Protocol
http://www.gridinsight.com/community/documentation/itron-ert-technology/

 Units: Some meter types transmit consumption in 1 kWh units, while others use more granular 10 Wh units


                           length     Offset
        Preamble        	2	
        Sync Word       	2       0
        Protocol ID     	1	2
        Packet Length   	1	3
        Hamming Code    	1	4
        Application Version	1       5
        Endpoint Type   	1	6
        Endpoint ID     	4       7
        Consumption Interval    1	11	
        Programming State	1       12
        Unknown_1       	13      13
        Last Generation Count	3	26
        Unknown_2       	3       29
        Last Consumption Count	4	32
        Differential Cons	48	36	27 intervals of 14-bit unsigned integers.
        Transmit Time Offset	2	84
        Meter ID Checksum	2	86	CRC-16-CCITT of Meter ID.
        Packet Checksum 	2	88	CRC-16-CCITT of packet starting at Packet Type.

*/

#define IDM_PACKET_BYTES 92
#define IDM_PACKET_BITLEN 720
// 92 * 8

static int netidm_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t b[92];
    data_t *data;
    unsigned sync_index;
    const uint8_t idm_frame_sync[] = {0x16, 0xA3, 0x1C};

    uint8_t PacketTypeID;
    char PacketTypeID_str[5];
    uint8_t PacketLength;
    // char    PacketLength_str[5];
    uint8_t HammingCode;
    // char    HammingCode_str[5];
    uint8_t ApplicationVersion;
    // char    ApplicationVersion_str[5];
    uint8_t ERTType;
    // char    ERTType_str[5];
    uint32_t ERTSerialNumber;
    uint8_t ConsumptionIntervalCount;
    uint8_t ModuleProgrammingState;
    // char  ModuleProgrammingState_str[5];

    uint8_t Unknown_field_1[13];
    char Unknown_field_1_str[32];

    uint32_t LastGenerationCount = 0;
    char LastGenerationCount_str[16];

    uint8_t Unknown_field_2[3];
    char Unknown_field_2_str[9];

    uint32_t LastConsumptionCount;
    char LastConsumptionCount_str[16];

    // uint64_t TamperCounters = 0;  // 6 bytes
    // char TamperCounters_str[16];
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

    if (decoder->verbose && bitbuffer->bits_per_row[0] > 600) {
        fprintf(stderr, "\n\n%s: rows=%d, row0 len=%hu\n", __func__, bitbuffer->num_rows, bitbuffer->bits_per_row[0]);
    }

    if (bitbuffer->bits_per_row[0] < IDM_PACKET_BITLEN) {

        // to be removed later
        if (decoder->verbose && bitbuffer->bits_per_row[0] > 600) {
            fprintf(stderr, "%s: %s, row len=%hu < %hu\n", __func__, "DECODE_ABORT_LENGTH",
                    bitbuffer->bits_per_row[0], IDM_PACKET_BITLEN);
            fprintf(stderr, "%s: DECODE_ABORT_LENGTH 1 %d < %d\n", __func__, bitbuffer->bits_per_row[0], IDM_PACKET_BITLEN);
        }
        // bitbuffer_print(bitbuffer);
        return (DECODE_ABORT_LENGTH);
    }

    sync_index = bitbuffer_search(bitbuffer, 0, 0, idm_frame_sync, 24);

    if (decoder->verbose) {
        fprintf(stderr, "%s: sync_index=%d\n", __func__, sync_index);
    }

    if (sync_index >= bitbuffer->bits_per_row[0]) {

        // to be removed later
        if (decoder->verbose) {
            fprintf(stderr, "%s: DECODE_ABORT_EARLY s > l\n", __func__);
            bitbuffer_print(bitbuffer);
        }
        return DECODE_ABORT_EARLY;
    }

    if ((bitbuffer->bits_per_row[0] - sync_index) < IDM_PACKET_BITLEN) {
        if (decoder->verbose) {
            fprintf(stderr, "%s: DECODE_ABORT_LENGTH 2 %d < %d\n", __func__, (bitbuffer->bits_per_row[0] - sync_index), IDM_PACKET_BITLEN);
            //  bitrow_printf(b, bitbuffer->bits_per_row[0], "%s bitrow_printf", __func__);
            bitbuffer_print(bitbuffer);
        }
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, sync_index, b, IDM_PACKET_BITLEN);
    if (decoder->verbose)
        bitrow_printf(b, IDM_PACKET_BITLEN, "%s bitrow_printf", __func__);

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

    HammingCode = b[4];
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

    //  should this be included ?
    p = Unknown_field_1_str;
    strncpy(p, "0x", sizeof(Unknown_field_1_str) - 1);
    p += 2;
    for (int j = 0; j < 13; j++) {
        p += sprintf(p, "%02X", b[13 + j]);
    }
    if (decoder->verbose) {
        bitrow_printf(&b[13], 13 * 8, "%s Unknown_field_1 %s\t", __func__, Unknown_field_1_str);
        bitrow_debug(&b[13], 13 * 8);
    }

    // 3 bytes
    LastGenerationCount = ((uint32_t)(b[26] << 16)) | (b[27] << 8) | (b[28]);

    //  should this be included ?
    p = Unknown_field_2_str;
    strncpy(p, "0x", sizeof(Unknown_field_2_str) - 1);
    p += 2;
    for (int j = 0; j < 3; j++) {
        p += sprintf(p, "%02X", b[29 + j]);
    }
    if (decoder->verbose)
        bitrow_printf(&b[29], 3 * 8, "%s Unknown_field_1 %s\t", __func__, Unknown_field_2_str);

    LastConsumptionCount = ((uint32_t)b[32] << 24) | (b[33] << 16) | (b[34] << 8) | (b[35]);

    if (decoder->verbose)
        bitrow_printf(&b[32], 32, "%s LastConsumptionCount %d\t", __func__, LastConsumptionCount);

    // DifferentialConsumptionIntervals[] = 27 intervals of 14-bit unsigned integers.
    unsigned pos = sync_index + (36 * 8);
    if (decoder->verbose)
        bitrow_printf(&b[36], 48 * 8, "%s DifferentialConsumptionIntervals", __func__);
    for (int j = 0; j < 27; j++) {
        uint8_t buffy[4] = {0};

        bitbuffer_extract_bytes(bitbuffer, 0, pos, buffy, 14);
        DifferentialConsumptionIntervals[j] = ((uint16_t)buffy[0] << 6) | (buffy[1] >> 2);
        // bitrow_printf(buffy, 14, "%d\t%d\t", j, DifferentialConsumptionIntervals[j]);
        pos += 14;
    }
    if (decoder->verbose) {
        fprintf(stderr, "%s DifferentialConsumptionIntervals:\n\t", __func__);
        for (int j = 0; j < 27; j++) {
            fprintf(stderr, "%d ", DifferentialConsumptionIntervals[j]);
        }
        fprintf(stderr, "\n\n");
        // bitrow_debug(&b[36], 48*8);
    }

    TransmitTimeOffset = (b[84] << 8 | b[85]);

    MeterIdCRC = (b[86] << 8 | b[87]);
    // snprintf(MeterIdCRC_str, sizeof(MeterIdCRC_str), "0x%04X", MeterIdCRC);

    // Least significant nibble of endpoint_type is  equivalent to SCM's endpoint type field
    // id info from https://github.com/bemasher/rtlamr/wiki/Compatible-Meters
    char *meter_type;
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
    // fprintf(stderr, "meter_type = %s\n", meter_type);

    /*
        Field key names and format set to  match rtlamr field names

        Time":"2020-06-25T08:22:08.569276915-04:00","Offset":1605632,"Length":229376,"Type":"NetIDM","Message":
        {"Preamble":1431639715,"ProtocolID":28,"PacketLength":92,"HammingCode":198,"ApplicationVersion":4,"ERTType":7,"ERTSerialNumber":1550406067,"ConsumptionIntervalCount":30,"ProgrammingState":184,"LastGeneration":125,"LastConsumption":0,"LastConsumptionNet":2223120656,"DifferentialConsumptionIntervals":[7695,545,2086,1475,6240,2180,4240,4616,240,7191,609,7224,1603,96,2052,12464,6152,8480,9226,352,12312,833,10292,1795,4248,4613,8416],"TransmitTimeOffset":2145,"SerialNumberCRC":61178,"PacketCRC":37271}}

    */

    /* clang-format off */
    data = data_make(
            "model",                     "",                 DATA_STRING, "NETIDM",

            "PacketTypeID",             "",             DATA_STRING,       PacketTypeID_str,
            "PacketLength",             "",             DATA_INT,       PacketLength,
            // "HammingCode",              "",             DATA_FORMAT, "0x%02X", DATA_INT, HammingCode,
            "ApplicationVersion",               "",     DATA_INT,       ApplicationVersion,

            "ERTType",                          "",     DATA_FORMAT,  "0x%02X", DATA_INT,    ERTType,
            "ERTSerialNumber",                  "",     DATA_INT,       ERTSerialNumber,
            "ConsumptionIntervalCount",         "",     DATA_INT,       ConsumptionIntervalCount,
            "ModuleProgrammingState",           "",     DATA_FORMAT, "0x%02X", DATA_INT, ModuleProgrammingState,
            // "ModuleProgrammingState",           "",     DATA_STRING,    ModuleProgrammingState_str,
            // "TamperCounters",                   "",     DATA_STRING,       TamperCounters_str,
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
            "mic",             "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",

        "PacketTypeID",
        "PacketLength",
        "HammingCode",
        "ApplicationVersion",
        "ERTType",
        "ERTSerialNumber",
        "ConsumptionIntervalCount",
        "ModuleProgrammingState",
        // "TamperCounters",
        // "AsynchronousCounters",
        "Unknown_field_1",
        "LastGenerationCount",
        "Unknown_field_2",
        "LastConsumptionCount",
        "DifferentialConsumptionIntervals",
        "TransmitTimeOffset",
        "MeterIdCRC",
        "PacketCRC",
        // "BARBARBAR",

        "MeterType",
        "mic",
        NULL,
};
//      Freq 912600155
//     -X n=L58,m=OOK_MC_ZEROBIT,s=30,l=30,g=20000,r=20000,match={24}0x16a31e,preamble={1}0x00

r_device netidm = {
        .name        = "Interval Data Message (IDM) for Net Meters",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 30,
        .long_width  = 30,
        .gap_limit   = 20000,
        .reset_limit = 20000,
        // .gap_limit   = 2500,
        // .reset_limit = 4000,
        .decode_fn = &netidm_callback,
        .disabled  = 0,
        .fields    = output_fields,
};
