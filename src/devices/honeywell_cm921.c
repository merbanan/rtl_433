/** @file
    Honeywell CM921 Thermostat.

    Copyright (C) 2020 Christoph M. Wintersteiger <christoph@winterstiger.at>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Honeywell CM921 Thermostat (subset of Evohome).

868Mhz FSK, PCM, Start/Stop bits, reversed, Manchester.
*/

#include "decoder.h"

#define _DEBUG

static int decode_10to8(uint8_t const *b, int pos, int end, uint8_t *out)
{
    // we need 10 bits
    if (pos + 10 > end)
        return DECODE_ABORT_LENGTH;

    // start bit of 0
    if (bitrow_get_bit(b, pos) != 0)
        return DECODE_FAIL_SANITY;

    // stop bit of 1
    if (bitrow_get_bit(b, pos + 9) != 1)
        return DECODE_FAIL_SANITY;

    *out = bitrow_get_byte(b, pos + 1);

    return 10;
}

typedef struct {
    uint8_t header;
    uint8_t num_device_ids;
    uint8_t device_id[4][3];
    uint8_t seq; 
    uint16_t command;
    uint8_t payload_length;
    uint8_t payload[256];
    uint8_t unparsed_length;
    uint8_t unparsed[256];
    uint8_t crc;
} message_t;

#ifdef _DEBUG
static data_t *add_hex_string(data_t *data, const char *name, const uint8_t *buf, size_t buf_sz)
{
    if (buf && buf_sz > 0) {
        char tstr[256];
        bitrow_snprint(buf, buf_sz * 8, tstr, sizeof (tstr));
        data = data_append(data, name, "", DATA_STRING, tstr, NULL);
    }
    return data;
}
#endif

typedef struct {
    int t;
    const char s[4];
} dev_map_entry_t;

static const dev_map_entry_t device_map[] = {
        {.t = 1, .s = "CTL"},  // Controller
        {.t = 2, .s = "UFH"},  // Underfloor heating (HCC80, HCE80)
        {.t = 3, .s = "Thm"},  // HCW82
        {.t = 4, .s = "TRV"},  // Thermostatic radiator valve (HR80, HR91, HR92)
        {.t = 7, .s = "DHW"},  // DHW sensor (CS92)
        {.t = 10, .s = "OTB"}, // OpenTherm bridge (R8810)
        {.t = 12, .s = "THm"}, // Thermostat with setpoint schedule control (DTS92E, CME921)
        {.t = 13, .s = "BDR"}, // Wireless relay box (BDR91) (HC60NG too?)
        {.t = 17, .s = " 17"}, // Dunno - Outside weather sensor?
        {.t = 18, .s = "HGI"}, // Honeywell Gateway Interface (HGI80, HGS80)
        {.t = 22, .s = "THM"}, // Thermostat with setpoint schedule control (DTS92E)
        {.t = 30, .s = "GWY"}, // Gateway (e.g. RFG100?)
        {.t = 32, .s = "VNT"}, // (HCE80) Ventilation (Nuaire VMS-23HB33, VMN-23LMH23)
        {.t = 34, .s = "STA"}, // Thermostat (T87RF)
        {.t = 63, .s = "NUL"}, // No device
};

static void decode_device_id(const uint8_t device_id[3], char *buf, size_t buf_sz)
{
    int dev_type = device_id[0] >> 2;
    int dev_id   = (device_id[0] & 0x03) << 16 | (device_id[1] << 8) | device_id[2];

    char const *dev_name = " --";
    for (size_t i = 0; i < sizeof(device_map) / sizeof(dev_map_entry_t); i++)
        if (device_map[i].t == dev_type)
            dev_name = device_map[i].s;

    snprintf(buf, buf_sz, "%3s:%06d", dev_name, dev_id);
}

static data_t *decode_device_ids(const message_t *msg, data_t *data, int style)
{
    char ds[64] = {0}; // up to 4 ids of at most 10+1 chars

    for (unsigned i = 0; i < msg->num_device_ids; i++) {
        if (i != 0)
            strcat(ds, " ");

        char buf[16] = {0};
        if (style == 0)
            decode_device_id(msg->device_id[i], buf, 16);
        else {
            sprintf(buf, "%02x%02x%02x",
                    msg->device_id[i][0],
                    msg->device_id[i][1],
                    msg->device_id[i][2]);
        }
        strcat(ds, buf);
    }

    return data_append(data, "ids", "Device IDs", DATA_STRING, ds, NULL);
}

#define UNKNOWN_IF(C) do { \
                        if (C) \
                           return data_append(data, "unknown", "", DATA_FORMAT, "%04x", DATA_INT, msg->command); \
                      } while (0)

static data_t *honeywell_cm921_interpret_message(r_device *decoder, const message_t *msg, data_t *data)
{
    // Sources of inspiration:
    // https://github.com/Evsdd/The-Evohome-Protocol/wiki
    // https://www.domoticaforum.eu/viewtopic.php?f=7&t=5806&start=30
    // (specifically https://www.domoticaforum.eu/download/file.php?id=1396)
    // https://github.com/zxdavb/ramses_protocol

    data = decode_device_ids(msg, data, 0);  // XXX IAP

    data_t *r = data;
    switch (msg->command) {
        case 0x1030: {
            UNKNOWN_IF(msg->payload_length != 16);
            data = data_append(data, "zone_idx", "", DATA_FORMAT, "%02x", DATA_INT, msg->payload[0], NULL);
            for (unsigned i = 0; i < 5; i++) { // order fixed?
                const uint8_t *p = &msg->payload[1 + 3*i];
                // *(p+1) == 0x01 always?
                int value = *(p+2);
                switch (*p) {
                    case 0xC8: data = data_append(data, "max_flow_temp", "", DATA_INT, value, NULL); break;
                    case 0xC9: data = data_append(data, "pump_run_time", "", DATA_INT, value, NULL); break;
                    case 0xCA: data = data_append(data, "actuator_run_time", "", DATA_INT, value, NULL); break;
                    case 0xCB: data = data_append(data, "min_flow_temp", "", DATA_INT, value, NULL); break;
                    case 0xCC: /* Unknown, always 0x01? */ break;
                    default:
                        decoder_logf(decoder, 1, __func__, "Unknown parameter to 0x1030: %x02d=%04d\n", *p, value);
                }
            }
            break;
        }
        case 0x313F: {
            UNKNOWN_IF(msg->payload_length != 1 && msg->payload_length != 9);
            switch (msg->payload_length) {
                case 1:
                    data = data_append(data, "time_request", "", DATA_INT, msg->payload[0], NULL); break;
                case 9: {
                    //uint8_t unknown_0 = msg->payload[0]; /* always == 0? */
                    //uint8_t unknown_1 = msg->payload[1]; /* direction? */
                    uint8_t second = msg->payload[2];
                    uint8_t minute = msg->payload[3];
                    //uint8_t day_of_week = msg->payload[4] >> 5;
                    uint8_t hour = msg->payload[4] & 0x1F;
                    uint8_t day = msg->payload[5];
                    uint8_t month = msg->payload[6];
                    uint8_t year[2] = { msg->payload[7],  msg->payload[8] };
                    char time_str[256];
                    sprintf(time_str, "%02d:%02d:%02d %02d-%02d-%04d", hour, minute, second, day, month, (year[0] << 8) | year[1]);
                    data = data_append(data, "datetime", "", DATA_STRING, time_str, NULL);
                    break;
                }
            }
            break;
        }
        case 0x0008: {
            UNKNOWN_IF(msg->payload_length != 2);
            data = data_append(data, "domain_id", "", DATA_INT, msg->payload[0], NULL);
            data = data_append(data, "demand", "", DATA_DOUBLE, msg->payload[1] / 200.0 /* 0xC8 */, NULL);
            break;
        }
        case 0x3ef0: {
            UNKNOWN_IF(msg->payload_length != 3 && msg->payload_length != 6);
            switch (msg->payload_length) {
                case 3:
                    data = data_append(data, "status", "", DATA_DOUBLE, msg->payload[1] / 200.0 /* 0xC8 */, NULL);
                    break;
                case 6:
                    data = data_append(data, "boiler_modulation_level", "", DATA_DOUBLE, msg->payload[1] / 200.0 /* 0xC8 */, NULL);
                    data = data_append(data, "flame_status", "", DATA_INT, msg->payload[3], NULL);
                    break;
            }
            break;
        }
        case 0x2309: {
            UNKNOWN_IF(msg->payload_length % 3 != 0);
            if (msg->payload_length == 3) { // for single zone use this message for backward compatibility
                data = data_append(data, "zone", "", DATA_INT, msg->payload[0], NULL);
                // Observation: CM921 reports a very high setpoint during binding (0x7eff); packet: 143255c1230903017efff7
                data = data_append(data, "setpoint", "", DATA_DOUBLE, ((msg->payload[1] << 8) | msg->payload[2]) / 100.0, NULL);
            }
            else
                for (size_t i=0; i < msg->payload_length; i+=3) {
                    char name[256];
                    snprintf(name, sizeof(name), "setpoint (zone %u)", msg->payload[i]);
                    int16_t temp = msg->payload[i+1] << 8 | msg->payload[i+2];
                    data = data_append(data, name, "", DATA_DOUBLE, temp/100.0, NULL);
            }
            break;
        }
        case 0x1100: {
            UNKNOWN_IF(msg->payload_length != 5 && msg->payload_length != 8);
            data = data_append(data, "domain_id", "", DATA_INT, msg->payload[0], NULL);
            data = data_append(data, "cycle_rate", "", DATA_DOUBLE, msg->payload[1] / 4.0, NULL);
            data = data_append(data, "minimum_on_time", "", DATA_DOUBLE, msg->payload[2] / 4.0, NULL);
            data = data_append(data, "minimum_off_time", "", DATA_DOUBLE, msg->payload[3] / 4.0, NULL);
            if (msg->payload_length == 8)
                data = data_append(data, "proportional_band_width", "", DATA_DOUBLE, (msg->payload[5] << 8 | msg->payload[6]) / 100.0, NULL);
            break;
        }
        case 0x0009: {
            UNKNOWN_IF(msg->payload_length != 3);
            data = data_append(data, "device_number", "", DATA_INT, msg->payload[0], NULL);
            switch (msg->payload[1]) {
                case 0: data = data_append(data, "failsafe_mode", "", DATA_STRING, "off", NULL); break;
                case 1: data = data_append(data, "failsafe_mode", "", DATA_STRING, "20-80", NULL); break;
                default: data = data_append(data, "failsafe_mode", "", DATA_STRING, "unknown", NULL);
            }
            break;
        }
        case 0x3B00: {
            UNKNOWN_IF(msg->payload_length != 2);
            data = data_append(data, "domain_id", "", DATA_INT, msg->payload[0], NULL);
            data = data_append(data, "state", "", DATA_DOUBLE, msg->payload[1] / 200.0 /* 0xC8 */, NULL);
            break;
        }
        case 0x30C9: {
          size_t num_zones = msg->payload_length/3;
          for (size_t i=0; i < num_zones; i++) {
            char name[256];
            snprintf(name, sizeof(name), "temp (zone %u)", msg->payload[3*i]);
            int16_t temp = msg->payload[3*i+1] << 8 | msg->payload[3*i+2];
            data = data_append(data, name, "", DATA_DOUBLE, temp/100.0, NULL);
          }
          break;
        }
        case 0x1fd4: {
            int temp = (msg->payload[1] << 8) | msg->payload[2];
            data = data_append(data, "ticker", "", DATA_INT, temp, NULL);
            break;
        }
        case 0x3150: {
            // example packet Heat Demand: 18 28ad9a 884dd3 3150 0200c6 88
            data = data_append(data, "zone", "", DATA_INT, msg->payload[0], NULL);
            data = data_append(data, "heat_demand", "", DATA_INT, msg->payload[1], NULL);
            break;
        }
        case 0x1f09: {
            // example "Packet" : "18045ef5045ef51f0903ff077693", "Header" : "18", "Seq" : "00", "Command" : "1f09", "Payload" : "ff0776",
            // IAP no idea of the function, appears to contain a temp
            UNKNOWN_IF(msg->payload_length != 3);
            data = data_append(data, "zone", "", DATA_INT, msg->payload[0], NULL);
            // Observation: CM921 reports a very high setpoint during binding (0x7eff); packet: 143255c1230903017efff7
            data = data_append(data, "setpoint", "", DATA_DOUBLE, ((msg->payload[1] << 8) | msg->payload[2]) / 100.0, NULL);
            break;
        }
        case 0x0002: {
            // Sent by a thermostat in resposne to a change in the aux wired input
            // "Packet" : "1a0da2520da2520300020403010105d1", "Header" : "1a", "Seq" : "03", "Command" : "0002", "Payload" : "03010105"
            // byte[2] of payload indicates whether aux input is logic 1 or 0
            // byte[0] is always 3 (remaining length?) 
            // byte[1] counts between 01 and 02 (seq always remians 0)
            // byte[3] is always 5
            UNKNOWN_IF(msg->payload_length != 4);
            data = data_append(data, "aux_input", "", DATA_INT, msg->payload[2], NULL);
            break;
        }
        case 0x2d49: {
            // Sent by CTL. 3 byte payload. Purpose unknown.
            // "Packet" : "18045ef5045ef52d490300b4000d", "Header" : "18", "Seq" : "00", "Command" : "2d49", "Payload" : "00b400"
            UNKNOWN_IF(msg->payload_length != 3);
            break;
        }
        case 0x2389: {
            // Sent by CTL. 3 byte payload. Purpose unknown.
            // "Packet" : "1a0da2520da252b723890302ffe697", "Header" : "1a", "Seq" : "b7", "Command" : "2389", "Payload" : "02ffe6"
            UNKNOWN_IF(msg->payload_length != 3);
            break;
        }
        case 0x000a: {
            UNKNOWN_IF(msg->payload_length % 6 != 0);
            for (size_t i=0; i < msg->payload_length; i+=6) {
                char name[256];
                int16_t temp;
                snprintf(name, sizeof(name), "flags (zone %u)", msg->payload[i]);
                temp = msg->payload[6*i+1];
                data = data_append(data, name, "", DATA_FORMAT, "%02x", DATA_INT, temp, NULL);
                snprintf(name, sizeof(name), "temp_low (zone %u)", msg->payload[i]);
                temp = msg->payload[6*i+2] << 8 | msg->payload[6*i+3];
                data = data_append(data, name, "", DATA_DOUBLE, temp/100.0, NULL);
                snprintf(name, sizeof(name), "temp_high (zone %u)", msg->payload[i]);
                temp = msg->payload[6*i+4] << 8 | msg->payload[6*i+5];
                data = data_append(data, name, "", DATA_DOUBLE, temp/100.0, NULL);
            }
            break;
        }
        default: /* Unknown command */
//printf("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX %04x\n",msg->command);
            return data_append(data, "unknown", "", DATA_FORMAT, "%04X", DATA_INT, 0xDEAD); 
            //UNKNOWN_IF(1);
    }
    return r;
}

static uint8_t next(const uint8_t *bb, unsigned *ipos, unsigned num_bytes)
{
    uint8_t r = bitrow_get_byte(bb, *ipos);
    *ipos += 8;
    if (*ipos >= num_bytes * 8)
        return DECODE_FAIL_SANITY;
    return r;
}

static int parse_msg(bitbuffer_t *bmsg, int row, message_t *msg)
{
    if (!bmsg || row >= bmsg->num_rows || bmsg->bits_per_row[row] < 8)
        return DECODE_ABORT_LENGTH;

    unsigned num_bytes = bmsg->bits_per_row[0]/8;
    unsigned num_bits = bmsg->bits_per_row[0];
    unsigned ipos = 0;
    const uint8_t *bb = bmsg->bb[row];
    memset(msg, 0, sizeof(message_t));

    // Checksum: All bytes add up to 0.
    int bsum = add_bytes(bb, num_bytes) & 0xff;
    int checksum_ok = bsum == 0;
    msg->crc = bitrow_get_byte(bb, bmsg->bits_per_row[row] - 8);

    if (!checksum_ok)
        return DECODE_FAIL_MIC;

    msg->header = next(bb, &ipos, num_bytes);

    msg->num_device_ids = msg->header == 0x14 ? 1 :
                          msg->header == 0x18 ? 2 :
                          msg->header == 0x1a ? 2 :
                          msg->header == 0x1c ? 2 :
                          msg->header == 0x10 ? 2 :
                          msg->header == 0x3c ? 2 :
                (msg->header >> 2) & 0x03; // total speculation.

    for (unsigned i = 0; i < msg->num_device_ids; i++)
        for (unsigned j = 0; j < 3; j++)
            msg->device_id[i][j] = next(bb, &ipos, num_bytes);

    if ( msg->header == 0x1a )
         msg->seq = next(bb, &ipos, num_bytes);
    else
         msg->seq = 0;

    msg->command = (next(bb, &ipos, num_bytes) << 8) | next(bb, &ipos, num_bytes);
    msg->payload_length = next(bb, &ipos, num_bytes);

    for (unsigned i = 0; i < msg->payload_length; i++)
        msg->payload[i] = next(bb, &ipos, num_bytes);

    if (ipos < num_bits - 8)
    {
      unsigned num_unparsed_bits = (bmsg->bits_per_row[row] - 8) - ipos;
      msg->unparsed_length = (num_unparsed_bits / 8) + (num_unparsed_bits % 8) ? 1 : 0;
      if (msg->unparsed_length != 0)
          bitbuffer_extract_bytes(bmsg, row, ipos, msg->unparsed, num_unparsed_bits);
    }

    return ipos;
}

static int honeywell_cm921_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Sources of inspiration:
    // https://www.domoticaforum.eu/viewtopic.php?f=7&t=5806&start=240

    // preamble=0x55 0xFF 0x00
    // preamble with start/stop bits=0101010101 0111111111 0000000001
    //                              =0101 0101 0101 1111 1111 0000 0000 01
    //                            =0x   5    5    5    F    F    0    0 4
    // post=10101100
    // each byte surrounded by start/stop bits (0byte1)
    // then manchester decode.
    const uint8_t preamble_pattern[4] = { 0x55, 0xff, 0x00, 0x40 }; // { 0x55, 0x5F, 0xF0, 0x04 };
    const uint8_t preamble_bit_length = 26; //30;
    const int row = 0; // we expect a single row only.
    int sep = 0;

    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[row] < 60) {
        decoder_logf(decoder, 1, __func__, "num_rows=%d, bits_per_row=%d", bitbuffer->num_rows, bitbuffer->bits_per_row[row]);
        return DECODE_ABORT_LENGTH;
    }

    decoder_log_bitrow(decoder, 1, __func__, bitbuffer->bb[row], bitbuffer->bits_per_row[row], "RX");

    int preamble_start = bitbuffer_search(bitbuffer, row, sep, preamble_pattern, preamble_bit_length);
    int start = preamble_start + preamble_bit_length;
    int len = bitbuffer->bits_per_row[row] - start;
    decoder_logf(decoder, 1, __func__, "preamble_start=%d start=%d len=%d", preamble_start, start, len);
    {
        int y;
        for(y=0;y<8;y++) {
            uint8_t tmp[1000];
            char tstr[200];
            memset(&tmp,0,1000);
            bitbuffer_extract_bytes(bitbuffer, 0, y,tmp, bitbuffer->bits_per_row[row]-y);
            bitrow_snprint(tmp, bitbuffer->bits_per_row[row]-y, tstr, sizeof (tstr));
            //decoder_logf(decoder, 1, __func__, "AA%d %s",y,tstr);
        }
    }
    if (len < 8 )
    {
        decoder_logf(decoder, 1, __func__, "err_runt");
        // continue;
        return DECODE_ABORT_LENGTH;
    }

    int end = start + len;
    bitbuffer_t bytes = {0};
    int pos = start;
    while (pos < end) {
        uint8_t byte = 0;
        if (decode_10to8(bitbuffer->bb[row], pos, end, &byte) != 10) {
            decoder_logf(decoder, 1, __func__, "err_8to10");
            break;
        }
        for (unsigned i = 0; i < 8; i++)
            bitbuffer_add_bit(&bytes, (byte >> i) & 0x1);
        pos += 10;
    }

    decoder_log_bitrow(decoder, 1, __func__, bytes.bb[0], bytes.bits_per_row[0], "BBB");

while( sep < bytes.bits_per_row[row])
{

    uint8_t header_pattern[3] = { 0x33, 0x55, 0x53 };
    int start33 = bitbuffer_search(&bytes, row, sep, header_pattern, sizeof(header_pattern)*8);

    if ( start33 != sep ) decoder_logf(decoder, 1, __func__, "Strange start33=%d",start33);

#if 0
    // Skip Manchester breaking header
    uint8_t header[3] = { 0x33, 0x55, 0x53 };
    if (bitrow_get_byte(bytes.bb[row], 0) != header[0] ||
        bitrow_get_byte(bytes.bb[row], 8) != header[1] ||
        bitrow_get_byte(bytes.bb[row], 16) != header[2])
    {
        decoder_logf(decoder, 1, __func__, "did not find 33 55 53");
        return DECODE_FAIL_SANITY;
    }

    // Find Footer 0x35 (0x55*)
    int fi = bytes.bits_per_row[row] - 8;
    int seen_aa = 0;
    while (bitrow_get_byte(bytes.bb[row], fi) == 0x55) {
        seen_aa = 1;
        fi -= 8;
    }
    if (!seen_aa || bitrow_get_byte(bytes.bb[row], fi) != 0x35)
    {
        decoder_logf(decoder, 1, __func__, "did not find 35 55+");
        return DECODE_FAIL_SANITY;
    }
#endif
    unsigned first_byte = start33+24;
    uint8_t seperator_pattern[2] = { 0xff, 0x00 };
    sep = bitbuffer_search(&bytes, row, first_byte, seperator_pattern, sizeof(seperator_pattern)*8);
    // if no seperator, this will be the end of the buffer

    if ( sep < bytes.bits_per_row[row] )
        decoder_logf(decoder, 1, __func__, "seperator at %d", sep);

    // Find Footer 0x35 (0x55*)
    unsigned fi = sep - 8;
    sep = sep + 16; // advance over seperator

    int seen_aa = 0;
    while (fi>8 && bitrow_get_byte(bytes.bb[row], fi) == 0x55) {
        seen_aa = 1;
        fi -= 8;
    }
    if (!seen_aa || bitrow_get_byte(bytes.bb[row], fi) != 0x35)
    {
        decoder_logf(decoder, 1, __func__, "did not find 35 55+");
        //return DECODE_FAIL_SANITY;
        continue;
    }


    //unsigned end_byte   = fi;
    //unsigned num_bits   = end_byte - first_byte;
    //unsigned num_bytes = num_bits/8 / 2;

    bitbuffer_t packet = {0};
    unsigned fpos = bitbuffer_manchester_decode(&bytes, row, first_byte, &packet, fi-first_byte); // decode as much as we can

    if ( fpos < fi )
    {
        decoder_logf(decoder, 1, __func__, "manchester_fail start=%d sep=%d fail_at=%d", first_byte, sep, fpos);

        char *bitrow_asprint_bits(uint8_t const *bitrow, unsigned bit_len);
        void free( void* ptr );

        char * p = bitrow_asprint_bits(bytes.bb[0], fpos);
        decoder_logf(decoder, 1, __func__, "USED: %s", p);
        free(p);
        p = bitrow_asprint_bits(bytes.bb[0],  bytes.bits_per_row[0]);
        decoder_logf(decoder, 1, __func__, "FULL: %s", p);
        free(p);
        //return DECODE_FAIL_SANITY;
        continue;
    }

    decoder_logf(decoder, 1, __func__, "cool %d bytes",packet.bits_per_row[0]/8);
    message_t message;
    int pr = parse_msg(&packet, 0, &message);

    /* clang-format off */
    data_t *data;
if (pr <= 0)
{
    data = data_make(
//XXX            "model",    "",             DATA_STRING, "Honeywell-CM921",
            "mic",      "Integrity",    DATA_STRING, "CSUM_FAIL",
            NULL);
    data = add_hex_string(data, "Packet", packet.bb[row], packet.bits_per_row[row] / 8);
    data = add_hex_string(data, "CRC", &message.crc, 1);
//    data = data_append(data, "# man errors", "", DATA_INT, man_errors, NULL);

    decoder_output_data(decoder, data);
    //return 1;
    continue;
}
else
{
    data = data_make(
            "mic",      "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
}
    /* clang-format on */

    data = honeywell_cm921_interpret_message(decoder, &message, data);

#ifdef _DEBUG
    data = add_hex_string(data, "Packet", packet.bb[row], packet.bits_per_row[row] / 8);
    data = add_hex_string(data, "Header", &message.header, 1);
    data = add_hex_string(data, "Seq", &message.seq, 1);
    uint8_t cmd[2] = {message.command >> 8, message.command & 0x00FF};
    data = add_hex_string(data, "Command", cmd, 2);
    data = add_hex_string(data, "Payload", message.payload, message.payload_length);
    data = add_hex_string(data, "Unparsed", message.unparsed, message.unparsed_length);
    data = add_hex_string(data, "CRC", &message.crc, 1);
    //data = data_append(data, "# man errors", "", DATA_INT, man_errors, NULL);
#endif

    decoder_output_data(decoder, data);
}

    return 1;
}

static char *output_fields[] = {
        "model",
        "ids",
#ifdef _DEBUG
        "Packet",
        "Header",
        "Flags",
        "Command",
        "Payload",
        "Unparsed",
        "CRC",
        "Seq",
        "# man errors",
#endif
        "unknown",
        "time_request",
        "flame_status",
        "zone",
        "setpoint",
        "cycle_rate",
        "minimum_on_time",
        "minimum_off_time",
        "proportional_band_width",
        "device_number",
        "failsafe_mode",
        "ticker",
        "heat_demand",
        "boiler_modulation_level",
        "datetime",
        "domain_id",
        "state",
        "demand",
        "status",
        "zone_idx",
        "max_flow_temp",
        "pump_run_time",
        "actuator_run_time",
        "min_flow_temp",
        "mic",
        "aux_input",
        "temp (zone 0)",
        "temp (zone 1)",
        "temp (zone 2)",
        "temp (zone 3)",
        "temp (zone 4)",
        "temp (zone 5)",
        "temp (zone 6)",
        "temp (zone 7)",
        "temp (zone 8)",
        "temp (zone 9)",
        "temp (zone 10)",
        "temp (zone 11)",
        "temp (zone 12)",
        "temp (zone 13)",
        "temp (zone 14)",
        "temp (zone 15)",
        "setpoint (zone 0)",
        "setpoint (zone 1)",
        "setpoint (zone 2)",
        "setpoint (zone 3)",
        "setpoint (zone 4)",
        "setpoint (zone 5)",
        "setpoint (zone 6)",
        "setpoint (zone 7)",
        "setpoint (zone 8)",
        "setpoint (zone 9)",
        "setpoint (zone 10)",
        "setpoint (zone 11)",
        "setpoint (zone 12)",
        "setpoint (zone 13)",
        "setpoint (zone 14)",
        "setpoint (zone 15)",
        "temp_low (zone 0)",
        "temp_low (zone 1)",
        "temp_low (zone 2)",
        "temp_low (zone 3)",
        "temp_low (zone 4)",
        "temp_low (zone 5)",
        "temp_low (zone 6)",
        "temp_low (zone 7)",
        "temp_low (zone 8)",
        "temp_low (zone 9)",
        "temp_low (zone 10)",
        "temp_low (zone 11)",
        "temp_low (zone 12)",
        "temp_low (zone 13)",
        "temp_low (zone 14)",
        "temp_low (zone 15)",
        "temp_high (zone 0)",
        "temp_high (zone 1)",
        "temp_high (zone 2)",
        "temp_high (zone 3)",
        "temp_high (zone 4)",
        "temp_high (zone 5)",
        "temp_high (zone 6)",
        "temp_high (zone 7)",
        "temp_high (zone 8)",
        "temp_high (zone 9)",
        "temp_high (zone 10)",
        "temp_high (zone 11)",
        "temp_high (zone 12)",
        "temp_high (zone 13)",
        "temp_high (zone 14)",
        "temp_high (zone 15)",
        "flags (zone 0)",
        "flags (zone 1)",
        "flags (zone 2)",
        "flags (zone 3)",
        "flags (zone 4)",
        "flags (zone 5)",
        "flags (zone 6)",
        "flags (zone 7)",
        "flags (zone 8)",
        "flags (zone 9)",
        "flags (zone 10)",
        "flags (zone 11)",
        "flags (zone 12)",
        "flags (zone 13)",
        "flags (zone 14)",
        "flags (zone 15)",
        NULL,
};

r_device honeywell_cm921 = {
        .name        = "Honeywell CM921 Wireless Programmable Room Thermostat",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 26,
        .long_width  = 26,
        .sync_width  = 0,
        .tolerance   = 5,
        .reset_limit = 2000,
        .decode_fn   = &honeywell_cm921_decode,
        .fields      = output_fields,
};
