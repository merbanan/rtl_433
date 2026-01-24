/** @file
    JCHENG SECURITY Contact and PIR sensors.

    Copyright (C) 2025 Giorgi Kotchlamazashvili

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
JCHENG SECURITY Contact Sensor - door/window contact sensor.

The sensor uses OOK PWM modulation:
- Short pulse: 400 us
- Long pulse: 1200 us
- Reset limit: 1260 us

Data layout (25 bits):

    PPPP IIII IIII IIII IIII OBSX XX

- P: 4 bit preamble (fixed 0xF)
- I: 16 bit sensor ID
- O: 1 bit is_on flag
- B: 1 bit battery_ok (1 = battery good)
- S: 1 bit state (1 = closed, 0 = open)
- X: 3 bit unknown/unused

Raw data is sent MSB first.
*/
static int jc_contact_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Look for repeated rows with 25 bits
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 25);
    if (row < 0) {
        return DECODE_ABORT_LENGTH;
    }

    if (bitbuffer->bits_per_row[row] != 25) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[row];

    // Check preamble: first 4 bits must be 0xF (1111)
    if ((b[0] >> 4) != 0x0F) {
        return DECODE_ABORT_EARLY;
    }

    // Extract 16-bit ID from bits 4-19
    // b[0] has preamble in high nibble, first nibble of ID in low nibble
    // b[1] has middle byte of ID
    // b[2] has last nibble of ID in high nibble
    int id = ((b[0] & 0x0F) << 12) | (b[1] << 4) | (b[2] >> 4);

    // Extract flags from bits 20-22 (in b[2] low nibble and b[3] high bits)
    // Bit 20 = is_on, Bit 21 = battery_ok, Bit 22 = state
    int is_on      = (b[2] >> 3) & 0x01;  // bit 20
    int battery_ok = (b[2] >> 2) & 0x01;  // bit 21
    int state      = (b[2] >> 1) & 0x01;  // bit 22 (1=closed, 0=open)

    /* clang-format off */
    data_t *data = data_make(
            "model",      "",           DATA_STRING, "Jcheng-Contact",
            "id",         "ID",         DATA_FORMAT, "%04x", DATA_INT, id,
            "closed",     "Closed",     DATA_INT,    state,
            "battery_ok", "Battery OK", DATA_INT,    battery_ok,
            "event",      "Event",      DATA_INT,    is_on,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const jc_contact_output_fields[] = {
        "model",
        "id",
        "closed",
        "battery_ok",
        "event",
        NULL,
};

r_device const jc_contact = {
        .name        = "JCHENG SECURITY Contact Sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 400,
        .long_width  = 1200,
        .gap_limit   = 1400,
        .reset_limit = 1260,
        .tolerance   = 341,
        .decode_fn   = &jc_contact_decode,
        .disabled    = 1, // disabled by default (no checksum)
        .fields      = jc_contact_output_fields,
};

/**
JCHENG SECURITY PassiveIR Sensor - PIR motion sensor.

The sensor uses OOK PWM modulation:
- Short pulse: 400 us
- Long pulse: 1200 us
- Reset limit: 12000 us

Data layout (25 bits):

    PPPP PPPP IIII IIII IIII TMBX XX

- P: 8 bit preamble (fixed 0xAA)
- I: 12 bit sensor ID
- T: 1 bit tamper flag
- M: 1 bit motion detected
- B: 1 bit battery_low (1 = low battery)
- X: 3 bit unknown/unused

Raw data is sent MSB first.
*/
static int jc_pir_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Look for repeated rows with 25 bits
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 25);
    if (row < 0) {
        return DECODE_ABORT_LENGTH;
    }

    if (bitbuffer->bits_per_row[row] != 25) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[row];

    // Check preamble: first byte must be 0xAA
    if (b[0] != 0xAA) {
        return DECODE_ABORT_EARLY;
    }

    // Extract 12-bit ID from bits 8-19
    int id = ((b[1] << 4) | (b[2] >> 4)) & 0x0FFF;

    // Extract flags from bits 20-22 (in b[2] low nibble)
    // Bit 20 = tamper, Bit 21 = motion, Bit 22 = battery_low
    int tamper      = (b[2] >> 3) & 0x01;  // bit 20
    int motion      = (b[2] >> 2) & 0x01;  // bit 21
    int battery_low = (b[2] >> 1) & 0x01;  // bit 22

    /* clang-format off */
    data_t *data = data_make(
            "model",       "",            DATA_STRING, "Jcheng-PIR",
            "id",          "ID",          DATA_FORMAT, "%03x", DATA_INT, id,
            "motion",      "Motion",      DATA_INT,    motion,
            "tamper",      "Tamper",      DATA_INT,    tamper,
            "battery_ok",  "Battery OK",  DATA_INT,    !battery_low,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const jc_pir_output_fields[] = {
        "model",
        "id",
        "motion",
        "tamper",
        "battery_ok",
        NULL,
};

r_device const jc_pir = {
        .name        = "JCHENG SECURITY PassiveIR Sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 400,
        .long_width  = 1200,
        .gap_limit   = 1400,
        .reset_limit = 12000,
        .tolerance   = 341,
        .decode_fn   = &jc_pir_decode,
        .disabled    = 1, // disabled by default (no checksum)
        .fields      = jc_pir_output_fields,
};
