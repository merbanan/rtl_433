/** @file
    Schrader TPMS protocol.

    Copyright (C) 2016 Benjamin Larsson
    and 2017 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Schrader TPMS decoder.

Packet payload: 1 sync nibble and 8 bytes data, 17 nibbles:

    0 12 34 56 78 9A BC DE F0
    7 f6 70 3a 38 b2 00 49 49
    S PF FI II II II PP TT CC

- S: sync
- P: preamble (0xf)
- F: flags
- I: id (28 bit)
- P: pressure from 0 bar to 6.375 bar, resolution of 25 mbar/hectopascal per bit
- T: temperature from -50 C to 205 C (1 bit = 1 temperature count 1 C)
- C: CRC8 from nibble 1 to E
*/

#include "decoder.h"

static int schraeder_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    uint8_t b[8];
    int serial_id;
    char id_str[9];
    int flags;
    char flags_str[3];
    int pressure;    // mbar/hectopascal
    int temperature; // deg C

    /* Reject wrong amount of bits */
    if (bitbuffer->bits_per_row[0] != 68)
        return 0;

    /* Shift the buffer 4 bits to remove the sync bits */
    bitbuffer_extract_bytes(bitbuffer, 0, 4, b, 64);

    /* Calculate the crc */
    if (b[7] != crc8(b, 7, 0x07, 0xf0)) {
        return 0;
    }

    /* Get data */
    serial_id   = (b[1] & 0x0F) << 24 | b[2] << 16 | b[3] << 8 | b[4];
    flags       = (b[0] & 0x0F) << 4 | b[1] >> 4;
    pressure    = b[5] * 25;
    temperature = b[6] - 50;
    sprintf(id_str, "%07X", serial_id);
    sprintf(flags_str, "%02x", flags);

    data = data_make(
            "model",            "",             DATA_STRING, "Schrader",
            "type",             "",             DATA_STRING, "TPMS",
            "flags",            "",             DATA_STRING, flags_str,
            "id",               "ID",           DATA_STRING, id_str,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure*0.1,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);

    decoder_output_data(decoder, data);
    return 1;
}

/**
TPMS Model: Schrader Electronics EG53MA4.
Contributed by: Leonardo Hamada (hkazu).

Probable packet payload:

    SSSSSSSSSS ???????? IIIIII TT PP CC

- S: sync
- ?: might contain the preamble, status and battery flags
- I: id (24 bits), could extend into flag bits (?)
- P: pressure, 25 mbar per bit
- T: temperature, degrees Fahrenheit
- C: checksum, sum of byte data modulo 256
*/
static int schrader_EG53MA4_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    uint8_t b[10];
    int serial_id;
    char id_str[9];
    unsigned flags;
    char flags_str[9];
    int pressure;    // mbar
    int temperature; // degree Fahrenheit
    int checksum;

    /* Check for incorrect number of bits received */
    if (bitbuffer->bits_per_row[0] != 120)
        return 0;

    /* Discard the first 40 bits */
    bitbuffer_extract_bytes(bitbuffer, 0, 40, b, 80);

    /* Calculate the checksum */
    checksum = (b[0]+b[1]+b[2]+b[3]+b[4]+b[5]+b[6]+b[7]+b[8]) & 0xff;
    if (checksum != b[9]) {
        return 0;
    }

    /* Get data */
    serial_id   = (b[4] << 16) | (b[5] << 8) | b[6];
    flags       = ((unsigned)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    pressure    = b[7] * 25;
    temperature = b[8];
    sprintf(id_str, "%06X", serial_id);
    sprintf(flags_str, "%08x", flags);

    data = data_make(
            "model",            "",             DATA_STRING, "Schrader Electronics EG53MA4",
            "type",             "",             DATA_STRING, "TPMS",
            "flags",            "",             DATA_STRING, flags_str,
            "id",               "ID",           DATA_STRING, id_str,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure*0.1,
            "temperature_F",    "Temperature",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, (double)temperature,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "type",
    "id",
    "flags",
    "pressure_kPa",
    "temperature_C",
    "mic",
    NULL
};

static char *output_fields_EG53MA4[] = {
    "model",
    "type",
    "id",
    "flags",
    "pressure_kPa",
    "temperature_F",
    "mic",
    NULL
};

r_device schraeder = {
    .name           = "Schrader TPMS",
    .modulation     = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_width    = 120,
    .long_width     = 0,
    .reset_limit    = 480,
    .decode_fn      = &schraeder_callback,
    .disabled       = 0,
    .fields         = output_fields,
};

r_device schrader_EG53MA4 = {
    .name           = "Schrader TPMS EG53MA4",
    .modulation     = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_width    = 123,
    .long_width     = 0,
    .reset_limit    = 236,
    .decode_fn      = &schrader_EG53MA4_callback,
    .disabled       = 0,
    .fields         = output_fields_EG53MA4,
};
