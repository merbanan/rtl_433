/** @file
    ERT SCM+ sensors.

    Copyright (C) 2020 Peter Shipley <peter.shipley@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ERT SCM+ sensors.

- Freq 912600155

Random information:

https://github.com/bemasher/rtlamr/wiki/Protocol
http://www.gridinsight.com/community/documentation/itron-ert-technology/

Units: "Some meter types transmit consumption in 1 kWh units, while others use more granular 10 Wh units"

*/

static int scmplus_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t b[16];
    data_t *data;
    unsigned sync_index;
    const uint8_t scmplus_frame_sync[] = {0x16, 0xA3, 0x1E};

    if (bitbuffer->bits_per_row[0] < 128) {
        return (DECODE_ABORT_LENGTH);
    }

    sync_index = bitbuffer_search(bitbuffer, 0, 0, scmplus_frame_sync, 24);

    if (sync_index >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }

    if ((bitbuffer->bits_per_row[0] - sync_index) < 128) {
        return DECODE_ABORT_LENGTH;
    }

    decoder_logf(decoder, 1, __func__, "row len=%hu sync_index=%u", bitbuffer->bits_per_row[0], sync_index);

    // bitbuffer_debug(bitbuffer);
    bitbuffer_extract_bytes(bitbuffer, 0, sync_index, b, 16 * 8);

    // uint32_t t_16; // temp vars
    // uint32_t t_32;

    uint16_t crc, pkt_checksum;
    // memcpy(&t_16, &b[14], 2);
    // pkt_checksum = ntohs(t_16);
    pkt_checksum = (b[14] << 8 | b[15]);

    crc = crc16(&b[2], 12, 0x1021, 0x0971);
    // decoder_logf(decoder, 0, __func__, "CRC = %d %04X == %d %04X", pkt_checksum,pkt_checksum,  crc, crc);
    if (crc != pkt_checksum) {
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 1, __func__, b, 16 * 8, "aligned");

    // uint8_t protocol_id;
    uint32_t endpoint_id;
    // uint8_t endpoint_type;
    uint32_t consumption_data;
    uint16_t physical_tamper;

    char crc_str[8];
    char protocol_id_str[5];
    char endpoint_type_str[5];
    char physical_tamper_str[8];

    // protocol_id = b[2];
    snprintf(protocol_id_str, sizeof(protocol_id_str), "0x%02X", b[2]); // protocol_id);  // b[2]

    // endpoint_type = b[3];
    snprintf(endpoint_type_str, sizeof(endpoint_type_str), "0x%02X", b[3]); // endpoint_type);  // b[3]

    // memcpy(&t_32, &b[4], 4);
    // endpoint_id = ntohl(t_32);
    endpoint_id = ((uint32_t)b[4] << 24) | (b[5] << 16) | (b[6] << 8) | (b[7]);

    // memcpy(&t_32, &b[8], 4);
    // consumption_data = ntohl(t_32);
    consumption_data = ((uint32_t)b[8] << 24) | (b[9] << 16) | (b[10] << 8) | (b[11]);

    // memcpy(&t_16, &b[12], 2);
    // physical_tamper = ntohs(t_16);
    // physical_tamper = ((t_16 & 0xFF00) >> 8 | (t_16 & 0x00FF) << 8);
    physical_tamper = (b[12] << 8 | b[13]);
    snprintf(physical_tamper_str, sizeof(physical_tamper_str), "0x%04X", physical_tamper);

    snprintf(crc_str, sizeof(crc_str), "0x%04X", crc);

    // Least significant nibble of endpoint_type is  equivalent to SCM's endpoint type field
    // id info from https://github.com/bemasher/rtlamr/wiki/Compatible-Meters
    char const *meter_type;

    switch (b[3] & 0x0f) {
    case 4:
    case 5:
    case 7:
    case 8:
        meter_type = "Electric";
        break;
    case 0:
    case 1:
    case 2:
    case 9:
    case 12:
        meter_type = "Gas";
        break;
    case 3:
    case 11:
    case 13:
        meter_type = "Water";
        break;
    default:
        meter_type = "unknown";
        break;
    }

    // decoder_logf(decoder, 0, __func__, "meter_type = %s", meter_type);

    /*
        Field key names and format set to  match rtlamr field names

        {Time:2020-06-20T09:58:19.074 Offset:49152 Length:49152
        SCM+:{ProtocolID:0x1E EndpointType:0xAB EndpointID:  68211547 Consumption:  6883 Tamper:0x4900 PacketCRC:0x39BE}}
    */

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "SCMplus",
            "id",               "",                 DATA_INT,    endpoint_id,
            "ProtocolID",       "Protocol_ID",      DATA_STRING, protocol_id_str, // TODO: this should be int
            "EndpointType",     "Endpoint_Type",    DATA_STRING, endpoint_type_str, // TODO: this should be int
            "EndpointID",       "Endpoint_ID",      DATA_INT,    endpoint_id, // TODO: remove this (see "id")
            "Consumption",      "",                 DATA_INT,    consumption_data,
            "Tamper",           "",                 DATA_STRING, physical_tamper_str, // TODO: should be int
            "PacketCRC",        "crc",              DATA_STRING, crc_str, // TODO: remove this
            "MeterType",        "Meter_Type",       DATA_STRING, meter_type,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "ProtocolID",
        "EndpointType",
        "EndpointID",
        "Consumption",
        "Tamper",
        "PacketCRC",
        "MeterType",
        "mic",
        NULL,
};

//      Freq 912600155
//     -X n=L58,m=OOK_MC_ZEROBIT,s=30,l=30,g=20000,r=20000,match={24}0x16a31e,preamble={1}0x00

r_device const scmplus = {
        .name        = "Standard Consumption Message Plus (SCMplus)",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 30,
        .long_width  = 0, // not used
        .gap_limit   = 0,
        .reset_limit = 64,
        .decode_fn   = &scmplus_decode,
        .fields      = output_fields,
};
