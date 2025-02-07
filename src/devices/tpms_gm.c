/** @file
    General Motors Aftermarket TPMS

    Copyright (C) 2025 Eric Blevins

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"
#include "r_util.h"
#include <stdio.h>
#include <inttypes.h>  // Needed for PRIX64

static uint8_t const preamble_pattern[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/**
Data was detected an initially captured using:

  rtl_433 -X 'n=name,m=OOK_MC_ZEROBIT,s=120,l=0,r=15600'

These sensors react to a signal from an EL-50448 relearn tool.
They enter into learn mode upon receiving the signal and transmit.
If they remain at 0PSI they will not transmit unless receiving signal
from the relearn tool.
If they have pressure they transmit every 2 minutes and will exit
the learn mode after one cycle.
Any sudden changes in pressure triggers immediate signal.
These do not seem capable of detection motion.



130 bits
AAAAAAAAAAAASSSSDDDDIIIIIIPPTTCCX
0000000000004c90007849176600536d0
 Data layout:

   AAAAAAAAAAAASSSSDDDDIIIIIIPPTTCCX

- A: preamble 0x000000000000
- S: Status
- D: Device type or prefix
- I: Device uniquie identifier
- P: Pressure
- T: Temperature
- C: CheckSum, modulo 256

The only status data detected is learn mode and low battery.
Bit 5 of status indicates low battery when set to 1
Bits 0,1,8 are set to 0 to indicate learn mode and 1 for operational mode.
The sensors drop to learn mode when detecting a large pressure drop
or when activated with the learning tool

In learn mode with zero pressure they only transmit when activated by
the learning tool.
Once presurized they will transmit in learn mode and within a couple
minutes switch to sending in operatioinal mode every teo minutes.

*/

static int tpms_gm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[0] != 130) {
        return DECODE_ABORT_LENGTH;
    }

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
    if ((computed_checksum % 256) != b[15]) {
        return DECODE_FAIL_MIC;
    }

    // Convert ID to an integer
    uint64_t sensor_id = ((uint64_t)b[8] << 32) | ((uint64_t)b[9] << 24) | (b[10] << 16) | (b[11] << 8) | b[12];
    uint16_t status = (b[6] << 8) | b[7];

    uint8_t pressure_raw = b[13];
    uint8_t temperature_raw = b[14];

    float pressure_kpa = (pressure_raw * 2.75) + 3.75;
    float pressure_psi = kpa2psi(pressure_kpa);
    float temperature_c = temperature_raw - 60;

    // Extract bits correctly based on little-endian order
    int bit8 = (status >> 8) & 1;
    int bit1 = (status >> 1) & 1;
    int bit0 = (status >> 0) & 1;

    // Status bits
    int learn_mode = ((bit8 == 0) && (bit1 == 0) && (bit0 == 0)) ? 1 : 0;
    int battery_ok = !((status >> 5) & 1);

    char status_hex[7];  // 6 chars + null terminator for "0xFFFF"
    snprintf(status_hex, sizeof(status_hex), "0x%04X", status);

    // **Convert raw 130-bit packet to hex string**
    char raw_hex[35];  // 130 bits → 17 bytes → 34 hex characters + null terminator
    for (int i = 0; i < 16; i++) {
        snprintf(&raw_hex[i * 2], 3, "%02X", b[i]);
    }

    // **Extract and append last 2 bits**
    uint8_t last_two_bits = (b[16] >> 6) & 0x03;  // Get the last two bits (bits 6 and 7 of byte 16)
    snprintf(&raw_hex[32], 3, "%X", last_two_bits);  // Append as a single hex digit

    /* clang-format off */
    data_t *data = data_make(
        "model",           "",            DATA_STRING,  "GM Aftermarket TPMS",
        "type",            "",            DATA_STRING,  "TPMS",
        "id",              "",            DATA_INT,     sensor_id,
        "status",          "",            DATA_STRING,  status_hex,
        "raw",             "Raw Data",    DATA_STRING,  raw_hex,
        "learn_mode",      "",            DATA_INT,     learn_mode,
        "battery_ok",      "",            DATA_INT,     battery_ok,
        "pressure_kPa",    "",            DATA_DOUBLE,  pressure_kpa,
        "pressure_PSI",    "",            DATA_DOUBLE,  pressure_psi,
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
    "status",
    "raw",
    "learn_mode",
    "battery_ok",
    "pressure_kPa",
    "pressure_PSI",
    "temperature_C",
    "mic",
    NULL,
};

r_device const tpms_gm = {
    .name        = "GM Aftermarket TPMS",
    .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_width = 120,
    .long_width  = 0,
    .reset_limit = 15600,
    .decode_fn   = &tpms_gm_decode,
    .fields      = output_fields,
};

