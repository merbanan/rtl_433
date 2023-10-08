/** @file
    TFA Dostmann Marbella (30.3238.06).

    Copyright (C) 2021 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
TFA Dostmann Marbella (30.3238.06).

Main display cat no: 3066.01
￼
￼External links
￼
￼https://www.tfa-dostmann.de/produkt/funk-poolthermometer-marbella-30-3066/
￼￼https://clientmedia.trade-server.net/1768_tfadost/media/3/52/21352.pdf

The Marbella sensor operates at 868MHz frequency band.
￼
FSK_PCM with 105 us long high durations

AA 2D D4 68 3F 16 0A 31 9A AA XX
PP SS SS RR RR RR ZC TT TA AA LL


P - preamble 0xA
S - common sync 0x2dd4
R - serial number of sensor
Z - always zero
C - 3 bit counter
T - 12 bit temperature in degree celsius
A - always 0xA
L - lsfr, byte reflected reverse galois with 0x31 key and generator
    7 bytes starting from the serial number
*/

#include "decoder.h"

static int tfa_marbella_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    unsigned bitpos = 0;
    uint8_t msg[11];

    uint8_t const preamble_pattern[] = {0xaa, 0x2d, 0xd4};

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof (preamble_pattern) * 8);

    if (bitpos == bitbuffer->bits_per_row[0])
        return DECODE_FAIL_SANITY;

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, sizeof(msg) * 8);

    if (msg[9] != 0xAA)
        return DECODE_FAIL_SANITY;

    // Rev-Galois with gen 0x31 and key 0x31
    uint8_t ic = lfsr_digest8_reflect(&msg[3], 7, 0x31, 0x31);
    if (ic != msg[10]) {
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitbuffer(decoder, 1, __func__, bitbuffer, "");

    int temp_raw = (msg[7] << 4) | (msg[8] >> 4);
    float temp_c = (temp_raw - 400) * 0.1f;
    int counter  = (msg[6] & 0xF) >> 1;
    int serialnr = msg[3] << 16 | msg[4] << 8 | msg[5];

    char serialnr_str[6 * 2 + 1];
    snprintf(serialnr_str, sizeof(serialnr_str), "%06x", serialnr);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "TFA-Marbella",
            "id",               "",             DATA_STRING, serialnr_str,
            "counter",          "",             DATA_INT,    counter,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "counter",
        "temperature_C",
        "mic",
        NULL,
};

r_device const tfa_marbella = {
        .name        = "TFA Marbella Pool Thermometer",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 105,
        .long_width  = 105,
        .reset_limit = 2000,
        .decode_fn   = &tfa_marbella_callback,
        .fields      = output_fields,
};
