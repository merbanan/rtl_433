/** @file
    Honeywell CM921 Thermostat.

    Copyright (C) 2020 Christoph M. Wintersteiger <christoph@winterstiger.at>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int honeywell_cm921_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Honeywell CM921 Thermostat (subset of Evohome).

868Mhz FSK, PCM, Start/Stop bits, reversed, Manchester.
*/

// #define _DEBUG

static int decode_10to8(uint8_t const *b, int pos, int end, uint8_t *out)
{
    // we need 10 bits
    if (pos + 10 > end) {
        return DECODE_ABORT_LENGTH;
    }

    // start bit of 0
    if (bitrow_get_bit(b, pos) != 0) {
        return DECODE_FAIL_SANITY;
    }

    // stop bit of 1
    if (bitrow_get_bit(b, pos + 9) != 1) {
        return DECODE_FAIL_SANITY;
    }

    *out = bitrow_get_byte(b, pos + 1);

    return 10;
}

typedef struct {
    uint8_t header;
    uint8_t num_device_ids;
    uint8_t device_id[4][3];
    uint16_t command;
    uint8_t payload_length;
    uint8_t payload[256];
    uint8_t unparsed_length;
    uint8_t unparsed[256];
    uint8_t crc;
} message_t;

/*
typedef struct {
    int t;
    const char s[4];
} dev_map_entry_t;

static const dev_map_entry_t device_map[] = {
        {.t = 1, .s = "CTL"},  // Controller
        {.t = 2, .s = "UFH"},  // Underfloor heating (HCC80, HCE80)
        {.t = 3, .s = " 30"},  // HCW82??
        {.t = 4, .s = "TRV"},  // Thermostatic radiator valve (HR80, HR91, HR92)
        {.t = 7, .s = "DHW"},  // DHW sensor (CS92)
        {.t = 10, .s = "OTB"}, // OpenTherm bridge (R8810)
        {.t = 12, .s = "THm"}, // Thermostat with setpoint schedule control (DTS92E, CME921)
        {.t = 13, .s = "BDR"}, // Wireless relay box (BDR91) (HC60NG too?)
        {.t = 17, .s = " 17"}, // Unknown - Outside weather sensor?
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
    for (size_t i = 0; i < sizeof(device_map) / sizeof(dev_map_entry_t); i++) {
        if (device_map[i].t == dev_type) {
            dev_name = device_map[i].s;
        }
    }

    snprintf(buf, buf_sz, "%3s:%06d", dev_name, dev_id);
}
*/

static uint8_t next(const uint8_t *bb, unsigned *ipos, unsigned num_bytes)
{
    uint8_t r = bitrow_get_byte(bb, *ipos);
    *ipos += 8;
    if (*ipos >= num_bytes * 8) {
        return DECODE_FAIL_SANITY;
    }
    return r;
}

static int parse_msg(bitbuffer_t *bmsg, int row, message_t *msg)
{
    if (!bmsg || row >= bmsg->num_rows || bmsg->bits_per_row[row] < 8) {
        return DECODE_ABORT_LENGTH;
    }

    unsigned num_bytes = bmsg->bits_per_row[0]/8;
    unsigned num_bits = bmsg->bits_per_row[0];
    unsigned ipos = 0;
    const uint8_t *bb = bmsg->bb[row];
    memset(msg, 0, sizeof(message_t));

    // Checksum: All bytes add up to 0.
    int bsum = add_bytes(bb, num_bytes) & 0xff;
    int checksum_ok = bsum == 0;
    msg->crc = bitrow_get_byte(bb, bmsg->bits_per_row[row] - 8);

    if (!checksum_ok) {
        return DECODE_FAIL_MIC;
    }

    msg->header = next(bb, &ipos, num_bytes);

    msg->num_device_ids = msg->header == 0x14 ? 1 :
                          msg->header == 0x18 ? 2 :
                          msg->header == 0x1c ? 2 :
                          msg->header == 0x10 ? 2 :
                          msg->header == 0x3c ? 2 :
                (msg->header >> 2) & 0x03; // total speculation.

    for (unsigned i = 0; i < msg->num_device_ids; i++) {
        for (unsigned j = 0; j < 3; j++) {
            msg->device_id[i][j] = next(bb, &ipos, num_bytes);
        }
    }

    msg->command = (next(bb, &ipos, num_bytes) << 8) | next(bb, &ipos, num_bytes);
    msg->payload_length = next(bb, &ipos, num_bytes);

    for (unsigned i = 0; i < msg->payload_length; i++) {
        msg->payload[i] = next(bb, &ipos, num_bytes);
    }

    if (ipos < num_bits - 8) {
        unsigned num_unparsed_bits = (bmsg->bits_per_row[row] - 8) - ipos;
        msg->unparsed_length = (num_unparsed_bits + 7) / 8;
        if (msg->unparsed_length != 0) {
            bitbuffer_extract_bytes(bmsg, row, ipos, msg->unparsed, num_unparsed_bits);
        }
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
    const uint8_t preamble_pattern[4] = { 0x55, 0x5F, 0xF0, 0x04 };
    const uint8_t preamble_bit_length = 30;
    const int row = 0; // we expect a single row only.

    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[row] < 60) {
        return DECODE_ABORT_LENGTH;
    }

    decoder_log_bitrow(decoder, 1, __func__, bitbuffer->bb[row], bitbuffer->bits_per_row[row], "");

    int preamble_start = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, preamble_bit_length);
    int start = preamble_start + preamble_bit_length;
    int len = bitbuffer->bits_per_row[row] - start;
    decoder_logf(decoder, 1, __func__, "preamble_start=%d start=%d len=%d", preamble_start, start, len);
    if (len < 8) {
        return DECODE_ABORT_LENGTH;
    }
    int end = start + len;

    bitbuffer_t bytes = {0};
    int pos = start;
    while (pos < end) {
        uint8_t byte = 0;
        if (decode_10to8(bitbuffer->bb[row], pos, end, &byte) != 10) {
            break;
        }
        for (unsigned i = 0; i < 8; i++) {
            bitbuffer_add_bit(&bytes, (byte >> i) & 0x1);
        }
        pos += 10;
    }

    // Skip Manchester breaking header
    uint8_t header[3] = { 0x33, 0x55, 0x53 };
    if (bitrow_get_byte(bytes.bb[row], 0) != header[0] ||
            bitrow_get_byte(bytes.bb[row], 8) != header[1] ||
            bitrow_get_byte(bytes.bb[row], 16) != header[2]) {
        return DECODE_FAIL_SANITY;
    }

    // Find Footer 0x35 (0x55*)
    int fi = bytes.bits_per_row[row] - 8;
    int seen_aa = 0;
    while (bitrow_get_byte(bytes.bb[row], fi) == 0x55) {
        seen_aa = 1;
        fi -= 8;
    }
    if (!seen_aa || bitrow_get_byte(bytes.bb[row], fi) != 0x35) {
        return DECODE_FAIL_SANITY;
    }

    unsigned first_byte = 24;
    unsigned end_byte   = fi;
    unsigned num_bits   = end_byte - first_byte;
    //unsigned num_bytes = num_bits/8 / 2;

    bitbuffer_t packet = {0};
    unsigned fpos = bitbuffer_manchester_decode(&bytes, row, first_byte, &packet, num_bits);
    unsigned man_errors = num_bits - (fpos - first_byte - 2);

#ifndef _DEBUG
    if (man_errors != 0) {
        return DECODE_FAIL_SANITY;
    }
#endif

    message_t msg;

    int pr = parse_msg(&packet, 0, &msg);

    if (pr <= 0) {
        return pr;
    }

    /* clang-format off */
    data_t *data = data_str(NULL, "model",    "",             NULL, "Honeywell-CM921");
    /* clang-format on */

    // Sources of inspiration:
    // https://github.com/Evsdd/The-Evohome-Protocol/wiki
    // https://www.domoticaforum.eu/viewtopic.php?f=7&t=5806&start=30
    // (specifically https://www.domoticaforum.eu/download/file.php?id=1396)

    // Decode Device IDs

    char ds[64] = {0}; // up to 4 ids of at most 10+1 chars

    for (unsigned i = 0; i < msg.num_device_ids; i++) {
        if (i != 0) {
            strcat(ds, " ");
        }

        char buf[16] = {0};
        // Unused alternative
        // decode_device_id(msg.device_id[i], buf, sizeof(buf));
        snprintf(buf, sizeof(buf), "%02x%02x%02x",
                msg.device_id[i][0],
                msg.device_id[i][1],
                msg.device_id[i][2]);
        strcat(ds, buf);
    }

    data = data_str(data, "ids", "Device IDs", NULL, ds);

    // Interpret Message

    switch (msg.command) {
    case 0x1030: {
        if (msg.payload_length != 16) {
            data = data_int(data, "unknown", "", "%04x", msg.command);
            break;
        }
        data = data_int(data, "zone_idx", "", "%02x", msg.payload[0]);
        for (unsigned i = 0; i < 5; i++) { // order fixed?
            const uint8_t *p = &msg.payload[1 + 3 * i];
            // *(p+1) == 0x01 always?
            int value = *(p + 2);
            switch (*p) {
            case 0xC8: data = data_int(data, "max_flow_temp", "", NULL, value); break;
            case 0xC9: data = data_int(data, "pump_run_time", "", NULL, value); break;
            case 0xCA: data = data_int(data, "actuator_run_time", "", NULL, value); break;
            case 0xCB: data = data_int(data, "min_flow_temp", "", NULL, value); break;
            case 0xCC: /* Unknown, always 0x01? */ break;
            default:
                decoder_logf(decoder, 1, __func__, "Unknown parameter to 0x1030: %x02d=%04d", *p, value);
            }
        }
        break;
    }
    case 0x313F: {
        if (msg.payload_length != 1 && msg.payload_length != 9) {
            data = data_int(data, "unknown", "", "%04x", msg.command);
            break;
        }
        switch (msg.payload_length) {
        case 1:
            data = data_int(data, "time_request", "", NULL, msg.payload[0]);
            break;
        case 9: {
            // uint8_t const unknown_0 = msg.payload[0]; /* always == 0? */
            // uint8_t const unknown_1 = msg.payload[1]; /* direction? */
            uint8_t const second = msg.payload[2];
            uint8_t const minute = msg.payload[3];
            // uint8_t const day_of_week = msg.payload[4] >> 5;
            uint8_t const hour    = msg.payload[4] & 0x1F;
            uint8_t const day     = msg.payload[5];
            uint8_t const month   = msg.payload[6];
            uint8_t const year[2] = {msg.payload[7], msg.payload[8]};
            char time_str[256];
            snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d %02d-%02d-%04d", hour, minute, second, day, month, (year[0] << 8) | year[1]);
            data = data_str(data, "datetime", "", NULL, time_str);
            break;
        }
        }
        break;
    }
    case 0x0008: {
        if (msg.payload_length != 2) {
            data = data_int(data, "unknown", "", "%04x", msg.command);
            break;
        }
        data = data_int(data, "domain_id", "", NULL, msg.payload[0]);
        data = data_dbl(data, "demand", "", NULL, msg.payload[1] * (1 / 200.0F) /* 0xC8 */);
        break;
    }
    case 0x3ef0: {
        if (msg.payload_length != 3 && msg.payload_length != 6) {
            data = data_int(data, "unknown", "", "%04x", msg.command);
            break;
        }
        switch (msg.payload_length) {
        case 3:
            data = data_dbl(data, "status", "", NULL, msg.payload[1] * (1 / 200.0F) /* 0xC8 */);
            break;
        case 6:
            data = data_dbl(data, "boiler_modulation_level", "", NULL, msg.payload[1] * (1 / 200.0F) /* 0xC8 */);
            data = data_int(data, "flame_status", "", NULL, msg.payload[3]);
            break;
        }
        break;
    }
    case 0x2309: {
        if (msg.payload_length != 3) {
            data = data_int(data, "unknown", "", "%04x", msg.command);
            break;
        }
        data = data_int(data, "zone", "", NULL, msg.payload[0]);
        // Observation: CM921 reports a very high setpoint during binding (0x7eff); packet: 143255c1230903017efff7
        data = data_dbl(data, "setpoint", "", NULL, ((msg.payload[1] << 8) | msg.payload[2]) * (1 / 100.0F));
        break;
    }
    case 0x1100: {
        if (msg.payload_length != 5 && msg.payload_length != 8) {
            data = data_int(data, "unknown", "", "%04x", msg.command);
            break;
        }
        data = data_int(data, "domain_id", "", NULL, msg.payload[0]);
        data = data_dbl(data, "cycle_rate", "", NULL, msg.payload[1] * (1 / 4.0F));
        data = data_dbl(data, "minimum_on_time", "", NULL, msg.payload[2] * (1 / 4.0F));
        data = data_dbl(data, "minimum_off_time", "", NULL, msg.payload[3] * (1 / 4.0F));
        if (msg.payload_length == 8)
            data = data_dbl(data, "proportional_band_width", "", NULL, (msg.payload[5] << 8 | msg.payload[6]) * (1 / 100.0F));
        break;
    }
    case 0x0009: {
        if (msg.payload_length != 3) {
            data = data_int(data, "unknown", "", "%04x", msg.command);
            break;
        }
        data = data_int(data, "device_number", "", NULL, msg.payload[0]);
        switch (msg.payload[1]) {
        case 0: data = data_str(data, "failsafe_mode", "", NULL, "off"); break;
        case 1: data = data_str(data, "failsafe_mode", "", NULL, "20-80"); break;
        default: data = data_str(data, "failsafe_mode", "", NULL, "unknown");
        }
        break;
    }
    case 0x3B00: {
        if (msg.payload_length != 2) {
            data = data_int(data, "unknown", "", "%04x", msg.command);
            break;
        }
        data = data_int(data, "domain_id", "", NULL, msg.payload[0]);
        data = data_dbl(data, "state", "", NULL, msg.payload[1] * (1 / 200.0F) /* 0xC8 */);
        break;
    }
    case 0x30C9: {
        size_t num_zones = msg.payload_length / 3;
        for (size_t i = 0; i < num_zones; i++) {
            char name[256];
            snprintf(name, sizeof(name), "temperature (zone %u)", msg.payload[3 * i]);
            int16_t temp = msg.payload[3 * i + 1] << 8 | msg.payload[3 * i + 2];
            data         = data_dbl(data, name, "", NULL, temp * (1 / 100.0F));
        }
        break;
    }
    case 0x1fd4: {
        int temp = (msg.payload[1] << 8) | msg.payload[2];
        data     = data_int(data, "ticker", "", NULL, temp);
        break;
    }
    case 0x3150: {
        // example packet Heat Demand: 18 28ad9a 884dd3 3150 0200c6 88
        data = data_int(data, "zone", "", NULL, msg.payload[0]);
        data = data_int(data, "heat_demand", "", NULL, msg.payload[1]);
        break;
    }
    default: /* Unknown command */
        data = data_int(data, "unknown", "", "%04x", msg.command);
        break;
    }

#ifdef _DEBUG
    char tstr[256];
    data = data_hex(data, "Packet", NULL, NULL, packet.bb[row], packet.bits_per_row[row] / 8, tstr);
    data = data_hex(data, "Header", NULL, NULL, &msg.header, 1, tstr);
    uint8_t cmd[2] = {msg.command >> 8, msg.command & 0x00FF};
    data = data_hex(data, "Command", NULL, NULL, cmd, 2, tstr);
    data = data_hex(data, "Payload", NULL, NULL, msg.payload, msg.payload_length, tstr);
    data = data_hex(data, "Unparsed", NULL, NULL, msg.unparsed, msg.unparsed_length, tstr);
    data = data_hex(data, "CRC", NULL, NULL, &msg.crc, 1, tstr);
    data = data_int(data, "# man errors", "", NULL, man_errors);
#endif

    /* clang-format off */
    data = data_str(data, "mic",      "Integrity",    NULL, "CHECKSUM");
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "ids",
#ifdef _DEBUG
        "Packet",
        "Header",
        "Command",
        "Payload",
        "Unparsed",
        "CRC",
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
        NULL,
};

r_device const honeywell_cm921 = {
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
