/** @file
    Decoder for TFA Dostmann 14.1504.V2 (30.3254.01)
    Radio-controlled grill and meat thermometer

    Copyright (C) 2022-2023 Joël Bourquard <joel.bourquard@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decoder for TFA Dostmann 14.1504.V2 (30.3254.01)

CAUTION: Do not confuse with TFA Dostmann 14.1504 (30.3201) which had a completely different protocol => [71] Maverick ET-732/733 BBQ Sensor

Payload format:
- Preamble         {36} 0x7aaaaaa5c (for robustness we only use the tail: {24}0xaaaa5c)
- Flags            {4}  OR between: 0x2=battery ok, 0x5=resync button
- Temperature      {12} Raw temperature value. Temperature in C = (value/4)-532. Example: 0x8a0 = 20 C
- Separator        {8}  0xff (differs if resync)
- Digest           {16} 16-bit LFSR digest + final XOR

To get raw data:

    rtl_433 -R 0 -X 'n=TFA-141504v2,m=FSK_PCM,s=360,l=360,r=4096,preamble={24}aaaa5c'

Example payloads (excluding preamble):
- Resync   = 7052f9cee3 (encoding differs from temperature readings => not handled)
- No probe = 2700ffb791 (just like a temperature reading => in this case we report the appropriate probe status and no temperature reading)
- ...
- 20 C     = 28a0ffce69
- 21 C     = 28a4ffa0f5
- ...
- 24 C     = 28b0ff6438
- ...
- 44 C     = 2900ff8c9d
- ...
*/

#define NUM_BITS_PREAMBLE 24
#define NUM_BYTES_DATA    5
#define OFFSET_MIC        (NUM_BYTES_DATA - 2)
#define NUM_BITS_DATA     (NUM_BYTES_DATA * 8)
#define NUM_BITS_TOTAL    (NUM_BITS_PREAMBLE + NUM_BITS_DATA)
#define NUM_BITS_MAX      (NUM_BITS_TOTAL + 12)

static int tfa_14_1504_v2_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xaa, 0xaa, 0x5c};

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }
    unsigned const row = 0;
    // signed value for safety
    int available_bits = bitbuffer->bits_per_row[row];

    // optional optimization: early exit if row too short
    if (available_bits < NUM_BITS_TOTAL) {
        return DECODE_ABORT_EARLY; // considered "early" because preamble not checked
    }

    // sync on preamble
    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, NUM_BITS_PREAMBLE);
    available_bits -= start_pos;
    if (available_bits < NUM_BITS_PREAMBLE) {
        return DECODE_ABORT_EARLY; // no preamble found
    }

    // check min & max length
    if (available_bits < NUM_BITS_TOTAL ||
            available_bits > NUM_BITS_MAX) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t data[NUM_BYTES_DATA];
    bitbuffer_extract_bytes(bitbuffer, row, start_pos + NUM_BITS_PREAMBLE, data, NUM_BITS_DATA);

    uint8_t flags = data[0] >> 4;
    // ignore resync button
    if ((flags & 0x5) == 0x5) {
        return DECODE_FAIL_SANITY;
    }
    unsigned battery_ok = (flags & 0x2) != 0;

    if (data[2] != 0xff) {
        return DECODE_FAIL_SANITY;
    }

    uint16_t calc_mic = lfsr_digest16(data, OFFSET_MIC, 0x8810, 0x0d42) ^ 0x16eb;
    uint16_t data_mic = (data[OFFSET_MIC] << 8) + data[OFFSET_MIC+1];
    if (calc_mic != data_mic) {
        return DECODE_FAIL_MIC;
    }

    // we discard the last 2 bits as those are always zero
    uint16_t raw_temp_c         = ((data[0] & 0xf) << 6) + (data[1] >> 2);
    unsigned is_probe_connected = (raw_temp_c != 0x1c0);
    float temp_c                = ((int)raw_temp_c) - 532;

    /* clang-format off */
    data_t *output = data_make(
            "model",            "",                 DATA_STRING,    "TFA-141504v2",
            "battery_ok",       "Battery",          DATA_INT,       battery_ok,
            "probe_fail",       "Probe failure",    DATA_INT,       !is_probe_connected,
            "temperature_C",    "Temperature",      DATA_COND,      is_probe_connected,     DATA_FORMAT,    "%.0f C",   DATA_DOUBLE,    temp_c,
            "mic",              "Integrity",        DATA_STRING,    "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, output);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "battery_ok",
        "probe_fail",
        "temperature_C",
        "mic",
        NULL,
};

r_device const tfa_14_1504_v2 = {
        .name        = "TFA Dostmann 14.1504.V2 Radio-controlled grill and meat thermometer",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 360,
        .long_width  = 360,
        .reset_limit = 4096,
        .decode_fn   = &tfa_14_1504_v2_decode,
        .fields      = output_fields,
};
