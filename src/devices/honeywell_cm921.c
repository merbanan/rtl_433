/** @file
    Honeywell CM921 Thermostat

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

// #define _DEBUG

static int get_byte(const bitbuffer_t *b, int row, int pos, int end, uint8_t *out)
{
    *out = 0;
    int bend = pos + 10;

    while (pos < bend) {
        if (pos >= end)
            return DECODE_ABORT_LENGTH;

        if (bitrow_get_bit(b->bb[row], pos++) != 0)
            return DECODE_FAIL_SANITY;

        if (pos + 8 >= end)
            return DECODE_ABORT_LENGTH;

        for (unsigned i = 0; i < 8; i++)
            *out = *out << 1 | bitrow_get_bit(b->bb[row], pos++);

        if (pos >= end)
            return DECODE_ABORT_LENGTH;

        if (bitrow_get_bit(b->bb[row], pos++) != 1)
            return DECODE_FAIL_SANITY;

        if (pos >= end)
            return DECODE_ABORT_LENGTH;
    }

    return 10;
}

uint8_t next(const uint8_t* bb, unsigned *ipos, unsigned num_bytes) {
    uint8_t r = bitrow_get_byte(bb, *ipos);
    *ipos += 8;
    if (*ipos >= num_bytes*8)
       return DECODE_FAIL_SANITY;
    return r;
}

typedef struct {
    uint8_t header;
    uint8_t num_device_ids : 2;
    uint8_t device_id[4][3];
    uint16_t command;
    uint8_t payload_length;
    uint8_t payload[256];
    uint8_t unparsed_length;
    uint8_t unparsed[256];
    uint8_t crc;
} message_t;


data_t *add_hex_string(data_t *data, const char *name, const uint8_t *buf, size_t buf_sz)
{
    if (buf && buf_sz > 0)  {
        char tstr[(buf_sz * 2) + 1];
        char *p = tstr;
        for (unsigned i = 0; i < buf_sz; i++, p+=2)
            sprintf(p, "%02x", buf[i]);
        *p = '\0';
        data = data_append(data, name, "", DATA_STRING, tstr, NULL);
    }
    return data;
}

typedef struct { uint8_t t; const char s[4]; } dev_map_entry_t;

static const dev_map_entry_t device_map[] = {
    { .t = 01, .s = "CTL" }, /* Controller */
    { .t = 02, .s = "UFH" }, /* Underfloor heating (HCC80, HCE80) */
    { .t = 03, .s = " 30" }, /* HCW82?? */
    { .t = 04, .s = "TRV" }, /* Thermostatic radiator valve (HR80, HR91, HR92) */
    { .t = 07, .s = "DHW" }, /* DHW sensor (CS92) */
    { .t = 10, .s = "OTB" }, /* OpenTherm bridge (R8810) */
    { .t = 12, .s = "THm" }, /* Thermostat with setpoint schedule control (DTS92E, CME921) */
    { .t = 13, .s = "BDR" }, /* Wireless relay box (BDR91) (HC60NG too?) */
    { .t = 17, .s = " 17" }, /* Dunno - Outside weather sensor? */
    { .t = 18, .s = "HGI" }, /* Honeywell Gateway Interface (HGI80, HGS80) */
    { .t = 22, .s = "THM" }, /* Thermostat with setpoint schedule control (DTS92E) */
    { .t = 30, .s = "GWY" }, /* Gateway (e.g. RFG100?) */
    { .t = 32, .s = "VNT" }, /* (HCE80) Ventilation (Nuaire VMS-23HB33, VMN-23LMH23) */
    { .t = 34, .s = "STA" }, /* Thermostat (T87RF) */
    { .t = 63, .s = "NUL" }, /* No device */
};

void decode_device_id(const uint8_t device_id[3], char *buf, size_t buf_sz)
{
    uint8_t dev_type = device_id[0] >> 2;
    const char *dev_name = " --";
    for (size_t i = 0; i < sizeof(device_map) / sizeof(dev_map_entry_t); i++)
        if (device_map[i].t == dev_type)
            dev_name = device_map[i].s;

    if (buf_sz < (6 + strlen(dev_name) + 1))
        return;

    sprintf(buf, "%3s:%06d", dev_name, (device_id[0] & 0x03) << 16 | (device_id[1] << 8) | device_id[2]);
}

data_t *decode_device_ids(const message_t *msg, data_t *data, int style) {
    char ds[64] = {0};

    for (unsigned i = 0; i < msg->num_device_ids; i++) {
        if (i != 0) strcat(ds, " ");

        char buf[256] = {0};
        if (style == 0)
            decode_device_id(msg->device_id[i], buf, 256);
        else {
            for (unsigned j = 0; j < 3; j++)
                sprintf(buf + 2*j, "%02x", msg->device_id[i][j]);
        }
        strcat(ds, buf);
    }

    return data_append(data, "Device IDs", "", DATA_STRING, ds, NULL);
}

#define UNKNOWN_IF(C) if (C) { return data_append(data, "unknown", "", DATA_FORMAT, "%04x", DATA_INT, msg->command, NULL); }

data_t *interpret_message(const message_t *msg, data_t *data, int verbose)
{
    /* Sources of inspiration:
       https://github.com/Evsdd/The-Evohome-Protocol/wiki
       https://www.domoticaforum.eu/viewtopic.php?f=7&t=5806&start=30
       (specifically https://www.domoticaforum.eu/download/file.php?id=1396) */

    data = decode_device_ids(msg, data, 1);

    data_t *r = data;
    switch(msg->command) {
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
                        if (verbose)
                            fprintf(stderr, "Unknown parameter to 0x1030: %x02d=%04d\n", *p, value);
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
            UNKNOWN_IF(msg->payload_length != 3);
            data = data_append(data, "zone", "", DATA_INT, msg->payload[0], NULL);
            // Observation: CM921 reports a very high setpoint during binding (0x7eff); packet: 143255c1230903017efff7
            data = data_append(data, "setpoint", "", DATA_DOUBLE, ((msg->payload[1] << 8) | msg->payload[2]) / 100.0, NULL);
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
            snprintf(name, sizeof(name), "temperature (zone %u)", msg->payload[3*i]);
            int16_t temp = msg->payload[3*i+1] << 8 | msg->payload[3*i+1];
            data = data_append(data, name, "", DATA_DOUBLE, temp/100.0, NULL);
          }
          break;
        }
        default: /* Unknown command */
            UNKNOWN_IF(1);
    }
    return r;
}

int parse_msg(bitbuffer_t *bmsg, int row, message_t *msg) {
    if (!bmsg || row >= bmsg->num_rows || bmsg->bits_per_row[row] < 8)
        return DECODE_ABORT_LENGTH;

    unsigned num_bytes = bmsg->bits_per_row[0]/8;
    unsigned num_bits = bmsg->bits_per_row[0];
    unsigned ipos = 0;
    const uint8_t *bb = bmsg->bb[row];
    memset(msg, 0, sizeof(message_t));

    // Checksum: All bytes add up to 0.
    uint8_t bsum = 0;
    for (unsigned i = 0; i < num_bytes; i++)
        bsum += bitrow_get_byte(bmsg->bb[row], i*8);
    uint8_t checksum_ok = bsum == 0;
    msg->crc = bitrow_get_byte(bmsg->bb[row], bmsg->bits_per_row[row] - 8);

    if (!checksum_ok)
        return DECODE_FAIL_MIC;

    msg->header = next(bb, &ipos, num_bytes);

    msg->num_device_ids = msg->header == 0x14 ? 1 :
                          msg->header == 0x18 ? 2 :
                          msg->header == 0x1c ? 2 :
                          msg->header == 0x10 ? 2 :
                          msg->header == 0x3c ? 2 :
                (msg->header >> 2) & 0x03; // total speculation.

    for (unsigned i = 0; i < msg->num_device_ids; i++)
        for (unsigned j = 0; j < 3; j++)
            msg->device_id[i][j] = next(bb, &ipos, num_bytes);

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
    const uint8_t preamble_pattern[4] = { 0x55, 0x5F, 0xF0, 0x04 };
    const uint8_t preamble_bit_length = 30;
    const int row = 0; // we expect a single row only.

    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[row] < 60)
        return DECODE_ABORT_LENGTH;

    if (decoder->verbose)
        bitrow_printf(bitbuffer->bb[row], bitbuffer->bits_per_row[row], "%s: ", __func__);

    int preamble_start = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, preamble_bit_length);
    int start = preamble_start + preamble_bit_length;
    int len = bitbuffer->bits_per_row[row] - start;
    if (decoder->verbose)
        fprintf(stderr, "preamble_start=%d start=%d len=%d\n", preamble_start, start, len);
    if (len < 8) return DECODE_ABORT_LENGTH;
    int end = start + len;

    bitbuffer_t bytes = {0};
    int pos = start;
    while (pos < end) {
        uint8_t byte = 0;
        if (get_byte(bitbuffer, row, pos, end, &byte) != 10)
            break;
        for (unsigned i = 0; i < 8; i++)
            bitbuffer_add_bit(&bytes, (byte >> i) & 0x1);
        pos += 10;
    }

    // Skip Manchester breaking header
    uint8_t header[3] = { 0x33, 0x55, 0x53 };
    if (bitrow_get_byte(bytes.bb[row], 0) != header[0] ||
        bitrow_get_byte(bytes.bb[row], 8) != header[1] ||
        bitrow_get_byte(bytes.bb[row], 16) != header[2])
        return DECODE_FAIL_SANITY;

    // Find Footer 0x35 (0x55*)
    int fi = bytes.bits_per_row[row] - 8;
    int seen_aa = 0;
    while (bitrow_get_byte(bytes.bb[row], fi) == 0x55) {
        seen_aa = 1;
        fi -= 8;
    }
    if (!seen_aa || bitrow_get_byte(bytes.bb[row], fi) != 0x35)
        return DECODE_FAIL_SANITY;

    unsigned first_byte = 24;
    unsigned end_byte = fi;
    unsigned num_bits  = end_byte - first_byte;
    //unsigned num_bytes = num_bits/8 / 2;

    bitbuffer_t packet = {0};
    unsigned fpos = bitbuffer_manchester_decode(&bytes, row, first_byte, &packet, num_bits);
    unsigned man_errors = num_bits  - (fpos - first_byte - 2);

#ifndef _DEBUG
    if (man_errors != 0)
        return DECODE_FAIL_SANITY;
#endif

    message_t message;

    int pr = parse_msg(&packet, row, &message);

    if (pr <= 0)
        return pr;

    /* clang-format off */
    data_t *data = data_make(
            "model",     "", DATA_STRING, "Honeywell CM921",
            "mic",    "MIC", DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    data = interpret_message(&message, data, decoder->verbose);

#ifdef _DEBUG
    data = add_hex_string(data, "Packet", packet.bb[row], packet.bits_per_row[row]/8);
    data = add_hex_string(data, "Header", &message.header, 1);
    uint8_t cmd[2] = { message.command >> 8, message.command & 0x00FF};
    data = add_hex_string(data, "Command", cmd, 2);
    data = add_hex_string(data, "Payload", message.payload, message.payload_length);
    data = add_hex_string(data, "Unparsed", message.unparsed, message.unparsed_length);
    data = add_hex_string(data, "CRC", &message.crc, 1);
    data = data_append(data, "# man errors", "", DATA_INT, man_errors, NULL);
#endif

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
        "model",
        "Device IDs",
#ifdef _DEBUG
        "Packet",
        "Header",
        "Command",
        "Payload",
        "Unparsed",
        "CRC",
        "# man errors",
#endif
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
        .disabled    = 0,
        .fields      = output_fields,
};
