/** @file
    ELV Energy Counter ESA 1000/2000.

    Copyright (C) 2016 TylerDurden23, initial cleanup by Benjamin Larsson
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define MAXMSG 40               // ESA messages

static uint16_t decrypt_esa(uint8_t *b, unsigned blen)
{
    uint8_t pos = 0;
    uint8_t salt = 0x89;
    uint16_t crc = 0;

    for (uint8_t i = 0; i < blen - 3; i++) {
        uint8_t byte = b[pos];
        crc += byte;
        b[pos++] ^= salt;
        salt = byte + 0x24;
    }
    uint8_t byte = b[pos];
    crc += byte;
    b[pos++] ^= 0xff;
    crc = ((b[blen - 2] << 8) | b[blen - 1]) - crc; // subtract from CRC at EOM
    return crc;
}

/**
    ELV Energy Counter ESA 1000/2000, GIRA EHZ.

    Bug fixes / modifications for GIRA WST:   D. Clawin  (DCTRONIC Engineering)

    @todo Documentation needed.

    ELV-ESA devices and Gira-EHZ share modulation and decryption. Only CRC base is different.

    ELVx000 data format, copied from FHEM source:

        ss                                          Sequenze und Sequenzwiederhohlung mit gesetzten h?chsten Bit
           dddd                                     Device
                cccc                                Code + Batterystate
                     tttttttt                       Gesamtimpules
                              aaaa                  Impulse je Sequenz
                                   zzzzzz           Zeitstempel seit Start des Adapters (ESA1000)
                                          kkkk      Impulse je kWh/m3

    GIRA data format (bytes), reverse engineered:

        H II SS J PP TTT II UUUUUU KK CC

    - H: Header with sequence number
    - I: Dev ID
    - S: Status and device type (same for all Gira cc1e)
    - J: Single byte: 04
    - P: power in Watts
    - T: Total ticks since startup
    - I: Ticks since last message
    - U: Unknown, appears always zero
    - K: Ticks / kWh xor 1st byte of devid
    - C: CRC, sum of message bytes + 0xee11
*/
static int esa_cost_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t b[MAXMSG];

    char *model;
    unsigned is_retry;
    unsigned sequence_id;
    unsigned deviceid;
    unsigned status = 0;
    unsigned power = 0;
    unsigned impulse_constant;
    unsigned impulses_val;
    unsigned impulses_total;
    float energy_total_val;
    float energy_impulse_val;
    unsigned len;

    len = bitbuffer->bits_per_row[0]; // # of bytes

    if ((!(len == 176 || len == 160)) || bitbuffer->num_rows != 1) {
        return DECODE_ABORT_LENGTH;
    }

    // remove first two bytes
    bitbuffer_extract_bytes(bitbuffer, 0, 16, b, len - 16);

    uint16_t crc = decrypt_esa(b, len / 8 - 2); // pass packet length w/o header in bytes
   
    if (crc == 0xf00f) { // ESA
        if (b[3] == 0x01) {
            model = "ESAx000WZ";
        }
        else if (b[3] == 0x03) {
            model = "ESA1000Z";
        }
        else {
            model = "ESA-unknown";
        }

        is_retry = (b[0] >> 7);
        sequence_id = (b[0] & 0x7f);
        deviceid = ((unsigned)b[1] << 8) | b[2];
        status = ((unsigned)b[3] << 8) | b[4]; // FHEM uses bit 7 as bat status
        impulses_val = (b[9] << 8) | b[10];
        impulses_total = ((unsigned)b[5] << 24) | (b[6] << 16) | (b[7] << 8) | b[8];
        impulse_constant = ((b[14] << 8) | b[15]) ^ b[1];
        energy_total_val = 1.0f * impulses_total / impulse_constant;
        energy_impulse_val = 1.0f * impulses_val / impulse_constant;
    }
    else if (crc == 0xee11) { // GIRA Wetterstation/Energiezaehler
        model = "Gira-EHZ";
        is_retry = (b[0] >> 6) & 0x01;
        sequence_id = (b[0] & 0x3f);
        deviceid = ((unsigned)b[1] << 8) | b[2];
        status = ((unsigned)b[3] << 8) | b[4];
        impulses_val = (b[11] << 8) | b[12];
        impulses_total = (((unsigned)b[8] << 16) | (b[9] << 8) | b[10]);
        impulse_constant = (b[16] << 8) | (b[17] ^ b[1]);
        energy_total_val = 1.0f * impulses_total / impulse_constant;
        energy_impulse_val = 1.0f * impulses_val / impulse_constant;
        power = (b[6] << 8) | b[7]; // Power in Watts, add-on for GIRA
    }
    else {
        decoder_logf(decoder, 1, __func__, "Bad CRC: %04x", crc);
        return DECODE_FAIL_MIC; // checksum fail
    }

    /* clang-format off */
    data = data_make(
            "model",            "Model",            DATA_STRING, model,
            "id",               "Id",               DATA_INT, deviceid,
            "impulses",         "Impulses",         DATA_INT, impulses_val,
            "impulses_total",   "Impulses Total",   DATA_INT, impulses_total,
            "impulse_constant", "Impulse Constant", DATA_INT, impulse_constant,
            "total_kWh",        "Energy Total",     DATA_DOUBLE, energy_total_val,
            "impulse_kWh",      "Energy Impulse",   DATA_DOUBLE, energy_impulse_val,
            "sequence_id",      "Sequence ID",      DATA_INT, sequence_id,
            "is_retry",         "Is Retry",         DATA_INT, is_retry,
            "status",           "Status/Type",       DATA_INT, status,
            "power",            "Power",            DATA_INT, power,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "impulses",
        "impulses_total",
        "impulse_constant",
        "total_kWh",
        "impulse_kWh",
        "sequence_id",
        "is_retry",
        "status",
        "power",
        "mic",
        NULL,
};

r_device const esa_energy = {
        .name        = "ESA1000 / ESA2000 Energy Monitor, GIRA Wetterstation",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 260,
        .long_width  = 0,
        .reset_limit = 3000,
        .decode_fn   = &esa_cost_callback,
        .disabled    = 1,
        .fields      = output_fields,
};
