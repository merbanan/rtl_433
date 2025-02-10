/** @file
    General Motors Aftermarket TPMS.

    Copyright (C) 2025 Eric Blevins

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
General Motors Aftermarket TPMS.

Data was detected and initially captured using:

    rtl_433 -X 'n=name,m=OOK_MC_ZEROBIT,s=120,l=0,r=15600'


Data layout, 130 bits:
    AAAAAAAAAAAAFFFFDDDDIIIIIIPPTTCCX
    0000000000004c90007849176600536d0


- A: preamble 0x000000000000
- F: Flags
- D: Device type or prefix
- I: Device uniquie identifier
- P: Pressure
- T: Temperature
- C: CheckSum, modulo 256

Format string:

    ID:10h FLAGS:4h KPA:2h TEMP:2h CHECKSUM:2h

The only status data detected is learn mode and low battery.
Bit 5 of status indicates low battery when set to 1.
Bits 0,1,8 are set to 0 to indicate learn mode and 1 for operational mode.
The sensors drop to learn mode when detecting a large pressure drop
or when activated with the EL-50448 learning tool.

In learn mode with zero pressure they only transmit when activated by
the learning tool.
Once presurized they will transmit in learn mode and within a couple
minutes switch to sending in operatioinal mode every two minutes.

*/

static int tpms_gm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[0] != 130) {
        return DECODE_ABORT_LENGTH;
    }

    static uint8_t const preamble_pattern[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    int pos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);
    if (pos < 0) {
        return DECODE_ABORT_EARLY;
    }

    // Buffer for extracted bytes
    uint8_t b[17] = {0};
    bitbuffer_extract_bytes(bitbuffer, 0, 0, b, 130);

    // Checksum skips preamble
    uint8_t computed_checksum = 0;
    for (int i = 6; i < 15; i++) {
        computed_checksum += b[i];
    }
    if ((computed_checksum & 0xFF) != b[15]) {
        return DECODE_FAIL_MIC;
    }

    // Convert ID to an integer
    uint64_t sensor_id = ((uint64_t)b[8] << 32) | ((uint64_t)b[9] << 24) | (b[10] << 16) | (b[11] << 8) | b[12];
    int flags     = (b[6] << 8) | b[7];

    int pressure_raw    = b[13];
    int temperature_raw = b[14];

    // Adding 3.75 made my sensors accurate
    // But I think it might be best to allow the user to
    // to add their own offset when consuming the data
    float pressure_kpa  = (pressure_raw * 2.75);
    float temperature_c = temperature_raw - 60;

    // Extract bits correctly based on little-endian order
    int bit8 = (flags >> 8) & 1;
    int bit1 = (flags >> 1) & 1;
    int bit0 = (flags >> 0) & 1;

    // Flags bits
    int learn_mode = ((bit8 == 0) && (bit1 == 0) && (bit0 == 0));
    int battery_ok = !((flags >> 5) & 1);

    /* clang-format off */
    data_t *data = data_make(
        "model",           "",            DATA_STRING,  "GM-Aftermarket",
        "type",            "",            DATA_STRING,  "TPMS",
        "id",              "",            DATA_INT,     sensor_id,
        "flags",           "",            DATA_INT,     flags,
        "learn_mode",      "",            DATA_INT,     learn_mode,
        "battery_ok",      "",            DATA_INT,     battery_ok,
        "pressure_kPa",    "",            DATA_DOUBLE,  pressure_kpa,
        "temperature_C",   "",            DATA_DOUBLE,  temperature_c,
        "mic",             "Integrity",   DATA_STRING,  "CHECKSUM",
        NULL);

    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** Output fields for rtl_433 */
static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "flags",
        "learn_mode",
        "battery_ok",
        "pressure_kPa",
        "temperature_C",
        "mic",
        NULL,
};

r_device const tpms_gm = {
        .name        = "GM-Aftermarket TPMS",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 120,
        .long_width  = 0,
        .reset_limit = 15600,
        .decode_fn   = &tpms_gm_decode,
        .fields      = output_fields,
};
