/** @file
    ERT IDM sensors.

    Copyright (C) 2020 Peter Shipley <peter.shipley@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <arpa/inet.h>
#include <byteswap.h>
#include <endian.h>
#include "decoder.h"

/**

Freq 912600155

Random information:

https://github.com/bemasher/rtlamr/wiki/Protocol
http://www.gridinsight.com/community/documentation/itron-ert-technology/

 Units: Some meter types transmit consumption in 1 kWh units, while others use more granular 10 Wh units

*/

#define IDM_PACKET_BYTES 92
#define IDM_PACKET_BITLEN 92 * 8

static int netidm_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t b[92];
    data_t *data;
    unsigned sync_index;
    const uint8_t idm_frame_sync[] = {0x16, 0xA3, 0x1C};

    uint8_t PacketTypeID;
    char    PacketTypeID_str[5];
    uint8_t PacketLength;
    char    PacketLength_str[5];
    uint8_t HammingCode;
    char    HammingCode_str[5];
    uint8_t ApplicationVersion;
    char    ApplicationVersion_str[5];
    uint8_t ERTType;
    char    ERTType_str[5];
    uint32_t ERTSerialNumber;
    uint8_t ConsumptionIntervalCount;
    uint8_t ModuleProgrammingState;
    char  ModuleProgrammingState_str[5];

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

    uint32_t DifferentialConsumptionIntervals[27] ={0};   // 27 intervals of 14-bit unsigned integers

    uint16_t TransmitTimeOffset;
    uint16_t MeterIdCRC;
    char  MeterIdCRC_str[8];
    uint16_t PacketCRC;
    char  PacketCRC_str[8];

    (void)fprintf(stderr, "\n\n%s: rows=%d, row0 len=%hu\n", __func__, bitbuffer->num_rows, bitbuffer->bits_per_row[0]);

    if (bitbuffer->bits_per_row[0] < IDM_PACKET_BITLEN) {
        if (decoder->verbose) 
            (void)fprintf(stderr, "%s: %s, row len=%hu < %hu\n", __func__, "DECODE_ABORT_LENGTH",
                bitbuffer->bits_per_row[0], IDM_PACKET_BITLEN);
        fprintf(stderr, "%s: DECODE_ABORT_LENGTH 1 %d < %d\n", __func__, bitbuffer->bits_per_row[0], IDM_PACKET_BITLEN);
        bitbuffer_print(bitbuffer);
        return (DECODE_ABORT_LENGTH);
    }

    sync_index = bitbuffer_search(bitbuffer, 0, 0, idm_frame_sync, 24);

    if (decoder->verbose) {
        (void)fprintf(stderr, "%s: sync_index=%d\n", __func__, sync_index);
    }

    if (sync_index >= bitbuffer->bits_per_row[0]) {
        fprintf(stderr, "%s: DECODE_ABORT_EARLY s > l\n", __func__);
        bitbuffer_print(bitbuffer);
        return DECODE_ABORT_EARLY;
    }

    if ( (bitbuffer->bits_per_row[0] - sync_index) < IDM_PACKET_BITLEN) {
        fprintf(stderr, "%s: DECODE_ABORT_LENGTH 2 %d < %d\n", __func__, (bitbuffer->bits_per_row[0] - sync_index), IDM_PACKET_BITLEN);
        //  bitrow_printf(b, bitbuffer->bits_per_row[0], "%s bitrow_printf", __func__);
        bitbuffer_print(bitbuffer);
        return DECODE_ABORT_LENGTH;
    }


    // bitbuffer_debug(bitbuffer);
    bitbuffer_extract_bytes(bitbuffer, 0, sync_index, b, IDM_PACKET_BITLEN);

    if (decoder->verbose) { // print bytes with aligned offset
        char payload[320] = {0};
        char *p          = payload;
        for (int j = 0; j < sizeof(b); j++) {
            p += sprintf(p, "%02X ", b[j]);
        }
        (void)fprintf(stderr, "%s: %s\n", __func__, payload);
    }

    uint32_t t_16; // temp vars
    uint32_t t_32;
    uint64_t t_64;
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

    snprintf(PacketCRC_str, sizeof(PacketCRC_str), "0x%04X", PacketCRC);
    // snprintf(XX_str, sizeof(XX_str), "0x%02X", XX);


    PacketTypeID = b[2];
    snprintf(PacketTypeID_str, sizeof(PacketTypeID_str), "0x%02X", PacketTypeID);

    PacketLength = b[3];
    snprintf(PacketLength_str, sizeof(PacketLength_str), "0x%02X", PacketLength);

    HammingCode = b[4];
    snprintf(HammingCode_str, sizeof(HammingCode_str), "0x%02X", HammingCode);

    ApplicationVersion = b[5];
    snprintf(ApplicationVersion_str, sizeof(ApplicationVersion_str), "0x%02X", ApplicationVersion);
    ERTType = b[6];
    snprintf(ERTType_str, sizeof(ERTType_str), "0x%02X", ERTType);

    // memcpy(&t_32, &b[7], 4);
    // ERTSerialNumber = ntohl(t_32);
    ERTSerialNumber = ((uint32_t)b[7] << 24) | (b[8] << 16) | (b[9] << 8) | (b[10]);

    ConsumptionIntervalCount = b[11];

    ModuleProgrammingState = b[12];
    snprintf(ModuleProgrammingState_str, sizeof(ModuleProgrammingState_str), "0x%02X", ModuleProgrammingState);

    p = Unknown_field_1_str;
    for(int j=0;j<13;j++){
        p += sprintf(p, "%02X", b[13+j]);
    }

    // 3 bytes 
    LastGenerationCount =  ((uint32_t) (b[26] << 16)) | (b[27] << 8) | (b[28]);

    p = Unknown_field_2_str;
    for(int j=0;j<3;j++){
        p += sprintf(p, "%02X", b[29+j]);
    }
    LastConsumptionCount = ((uint32_t)b[32] << 24) | (b[33] << 16) | (b[34] << 8) | (b[35]);
    memcpy(&t_32, &b[32], 4);
    bitrow_printf(&b[32], 4*8, "%s\t%2d %02X : %2d %02X\t", "LastConsumptionCount 4b",
        LastConsumptionCount, LastConsumptionCount, t_32, t_32);

    unsigned pos = sync_index + (36 * 8) ;
    // uint16_t *dci = DifferentialConsumptionIntervals;
    bitrow_printf(&b[36], 48*8, "DifferentialConsumptionIntervals");
    uint16_t tt;
                 fprintf(stderr, "           t_16       D[j]        tt        buffy0 1     buffy0<6 buffy1>2\n");
    for(int j=0;j<27;j++) {
        uint8_t buffy[4] = {0};

        bitbuffer_extract_bytes(bitbuffer, 0, pos, (uint8_t*) &tt , 14);
        // (void)fprintf(stderr, "tt =  %d %02X\n",  tt, tt);
        tt = bswap_16(tt);
        tt = tt >> 2;
        // (void)fprintf(stderr, "TT =  %d %02X\n",  tt, tt);
        bitbuffer_extract_bytes(bitbuffer, 0, pos, buffy, 14);
        memcpy(&t_16, buffy, 2);
        tt = ((uint16_t)buffy[0] << 6) | (buffy[1] >> 2);
        DifferentialConsumptionIntervals[j]=tt;
        bitrow_printf(buffy, 14, "Diff %2d : %5d %04X : %5d %04X : %5d %04X : %5d %5d :  %5d %5d", j,
            t_16, t_16, 
            DifferentialConsumptionIntervals[j], DifferentialConsumptionIntervals[j],
            tt, tt,
            buffy[0], buffy[1],
            (buffy[0] << 6), ( buffy[1] >> 2)
            );
        // dci++;
        pos += 14;
    }
    (void)fprintf(stderr, "DifferentialConsumptionIntervals:\n\t");
    for(int j=0;j<27;j++) {
        (void)fprintf(stderr, "%d ", DifferentialConsumptionIntervals[j]);
    }
    (void)fprintf(stderr, "\n\n");
    

    // memcpy(&t_16, &b[84], 2);
    // TransmitTimeOffset = bswap_16(t_16);
    TransmitTimeOffset = (b[84] << 8 | b[85]);

    // memcpy(&t_16, &b[86], 2);
    // MeterIdCRC = bswap_16(t_16);
    MeterIdCRC = (b[86] << 8 | b[87]);
    snprintf(MeterIdCRC_str, sizeof(MeterIdCRC_str), "0x%04X", MeterIdCRC);



    /* 
    char crc_str[8];
    char protocol_id_str[5];
    char endpoint_type_str[5];
    char physical_tamper_str[8];
    */

    // protocol_id = b[2];
    // snprintf(protocol_id_str, sizeof(protocol_id_str), "0x%02X", b[2]); // protocol_id);  // b[2]

    // endpoint_type = b[3];
    // snprintf(endpoint_type_str, sizeof(endpoint_type_str), "0x%02X", b[3]); // endpoint_type);  // b[3]


    //  consumption_data = ((uint32_t)b[8] << 24) | (b[9] << 16) | (b[10] << 8) | (b[11]);

    /*
    memcpy(&t_16, &b[12], 2);
    physical_tamper = ntohs(t_16);
    snprintf(physical_tamper_str, sizeof(physical_tamper_str), "0x%04X", physical_tamper);

    snprintf(crc_str, sizeof(crc_str), "0x%04X", crc);
    */

    /*
    if (decoder->verbose && 0) {
        (void)fprintf(stderr, "protocol_id = %d %02X\n", protocol_id,protocol_id);
        bitrow_printf(&b[3], 8, "%s\t%2d\t%02X\t", "endpoint_type   ", endpoint_type, endpoint_type);
        bitrow_printf(&b[4], 32, "%s\t%2d\t%02X\t", "endpoint_id    ", endpoint_id, endpoint_id);
        bitrow_printf(&b[8], 32, "%s\t%2d\t%02X\t", "consumption_data", consumption_data, consumption_data);
        // (void)fprintf(stderr, "consumption_data = %d %08X\n", consumption_data,consumption_data);
        (void)fprintf(stderr, "physical_tamper = %d %04X\n", physical_tamper,physical_tamper);
        (void)fprintf(stderr, "pkt_checksum = %d %04X\n", pkt_checksum,pkt_checksum);
    }
    */

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
    // (void)fprintf(stderr, "meter_type = %s\n", meter_type);

    /* 
        Field key names and format set to  match rtlamr field names 

        {Time:2020-06-20T09:58:19.074 Offset:49152 Length:49152
        SCM+:{ProtocolID:0x1E EndpointType:0xAB EndpointID:  68211547 Consumption:  6883 Tamper:0x4900 PacketCRC:0x39BE}}
    */

    /* clang-format off */
    data = data_make(
            "model",           "",                 DATA_STRING, "NETIDM",

            // "PacketTypeID",             "",             DATA_FORMAT, "0x%02x", DATA_INT, PacketTypeID,
            "PacketTypeID",             "",             DATA_STRING,       PacketTypeID_str,    
            "PacketLength",             "",             DATA_STRING,       PacketLength_str,
            "HammingCode",              "",             DATA_STRING,       HammingCode_str,
            "ApplicationVersion",               "",     DATA_STRING,       ApplicationVersion_str,
            "ERTType",                          "",     DATA_STRING,       ERTType_str,
            "ERTSerialNumber",                  "",     DATA_INT,       ERTSerialNumber,
            "ConsumptionIntervalCount",         "",     DATA_INT,       ConsumptionIntervalCount,
            // "ModuleProgrammingState",           "",     DATA_FORMAT, "0x%02x", DATA_INT, ModuleProgrammingState,
            "ModuleProgrammingState",           "",     DATA_STRING,    ModuleProgrammingState_str,
            // "TamperCounters",                   "",     DATA_STRING,       TamperCounters_str,
            // "AsynchronousCounters",             "",     DATA_FORMAT, "0x%02x", DATA_INT, AsynchronousCounters,
            "Unknown_field_1",                  "",     DATA_STRING,    Unknown_field_1_str,
            "LastGenerationCount",              "",     DATA_INT,       LastGenerationCount,
            "Unknown_field_2",                  "",     DATA_STRING,    Unknown_field_2_str,

            // "AsynchronousCounters",             "",     DATA_STRING,    AsynchronousCounters_str,

            // "PowerOutageFlags",                 "",     DATA_STRING,       PowerOutageFlags_str ,
            "LastConsumptionCount",             "",     DATA_INT,       LastConsumptionCount,
            "DifferentialConsumptionIntervals", "",     DATA_ARRAY, data_array(27, DATA_INT, DifferentialConsumptionIntervals),
            "TransmitTimeOffset",               "",     DATA_INT,       TransmitTimeOffset,
            "MeterIdCRC",                  "",     DATA_STRING,       MeterIdCRC_str,
            "PacketCRC",                        "",     DATA_STRING,       PacketCRC_str,

            "MeterType",       "Meter_Type",       DATA_STRING, meter_type,
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
        .decode_fn   = &netidm_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
