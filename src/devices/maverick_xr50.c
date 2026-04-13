/** @file
    Maverick XR-50 BBQ Sensor Europe Version.

    Copyright (C) 2025 Luca Pinasco

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Maverick XR-50 BBQ Sensor.

Examples:

    555555555555555a5545ba8100de0008343e9e001234821e000b543e9e0014a0ce4d401555400000
    555555555555555a5545ba8100de0008347d1e0008347d1e0008347d1e0008347d02c01555400000
    555555555555555a5545ba86811a5c2d5cc13a5e2d5cc1d85c89743e985b89883e80801555400

align preamble sync word:

    PP PP PP PP PP SS SS SS SS
    .. aa aa aa aa d2 aa 2d d4

Data layout after sync word:

    Byte Position  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 TT TT TT TT TT TT
    Sample        34 08 d2 e1 6a e6 09 d2 f1 6a e6 0e c2 e4 4b a1 f4 c2 dc 4c 41 f4 04 00 aa aa 00 0
    Sample        08 06 f0 00 41 a1 f4 f0 00 91 a4 10 f0 00 5a a1 f4 f0 00 a5 06 72 6a 00 aa aa 00 00 00
    Sample        08 06 f0 00 41 a3 e8 f0 00 41 a3 e8 f0 00 41 a3 e8 f0 00 41 a3 e8 16 00 aa aa 00 00 00
    Sample        08 06 f0 00 41 a1 f4 f0 00 91 a4 10 f0 00 5a a1 f4 f0 00 a5 06 72 6a 00 aa aa 00 00 00 [no probe]
    Layout        II II FT TT HH HL LL FT TT HH HL LL FT TT HH HL LL FT TT HH HL LL CC TT TT TT TT TT TT
                       [Probe 1      ][Probe 2      ][Probe 3      ][Probe 4      ]

- II:{16} Probably ID, 0x0806 or 0x3408 from above samples
- F:{4} Some flags, depends on the presence of probe, if temp below, within or above range
    0xF = No probe, in that case, the TTT = 0x000
    0xD = Temp below low temp value
    0xC = Temp within the range, between low and high temp values set.
    0x? all flags not yet guessed

- TTT:{12} Actual probe Temperature in Celsius, offset 500, scale 10
- HHH:{12} High Temperature set, Celsius, offset 500, scale 10
- LLL:{12} Low Temperature set, Celsius, offset 500, scale 10
- CC :{8}  CRC 8, poly 0x31, init 0x00, final XOR 0x00, from all previous 22 bytes.
*/
static int maverick_xr50_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = { 0xd2, 0xaa, 0x2d, 0xd4 };

    uint8_t b[23];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }

    int msg_len = bitbuffer->bits_per_row[0];
    int start_pos = bitbuffer_search(bitbuffer,0, 0, preamble, sizeof(preamble) * 8);

    if (start_pos  >= msg_len ) {
        decoder_log(decoder, 3, __func__, "Sync word not found");
        return DECODE_ABORT_LENGTH;
    }

    if ((msg_len - start_pos ) < 184 ) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    start_pos += sizeof(preamble) * 8;

    // Need 23 bytes, last bit are useless trailing bits
    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, b, 23 * 8);

    if (crc8(b, 23, 0x31, 0x00)) {
        decoder_logf(decoder, 1, __func__, "CRC Error, found: %02x, expected: %02x", b[22], crc8(b, 22, 0x31, 0x00));
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 1, __func__, b, 23 * 8, "MSG");

    int id = b[0] << 8 | b[1];

    int p_1_flags    = (b[2] & 0xf0) >> 4;
    int p_1_temp_raw = ((b[2] & 0x0f) << 8) | b[3];
    int p_1_high_raw = (b[4] << 4) | ((b[5] & 0xf0) >> 4);
    int p_1_low_raw  = ((b[5] & 0x0f) << 8) | b[6];

    int p_2_flags    = (b[7] & 0xf0) >> 4;
    int p_2_temp_raw = ((b[7] & 0x0f) << 8) | b[8];
    int p_2_high_raw = (b[9] << 4) | ((b[10] & 0xf0) >> 4);
    int p_2_low_raw  = ((b[10] & 0x0f) << 8) | b[11];

    int p_3_flags    = (b[12] & 0xf0) >> 4;
    int p_3_temp_raw = ((b[12] & 0x0f) << 8) | b[13];
    int p_3_high_raw = (b[14] << 4) | ((b[15] & 0xf0) >> 4);
    int p_3_low_raw  = ((b[15] & 0x0f) << 8) | b[16];

    int p_4_flags    = (b[17] & 0xf0) >> 4;
    int p_4_temp_raw = ((b[17] & 0x0f) << 8) | b[18];
    int p_4_high_raw = (b[19] << 4) | ((b[20] & 0xf0) >> 4);
    int p_4_low_raw  = ((b[20] & 0x0f) << 8) | b[21];

    float p1_temp     = (p_1_temp_raw - 500) * 0.1f;
    float p1_set_high = (p_1_high_raw - 500) * 0.1f;
    float p1_set_low  = (p_1_low_raw - 500) * 0.1f;

    float p2_temp     = (p_2_temp_raw - 500) * 0.1f;
    float p2_set_high = (p_2_high_raw - 500) * 0.1f;
    float p2_set_low  = (p_2_low_raw - 500) * 0.1f;

    float p3_temp     = (p_3_temp_raw - 500) * 0.1f;
    float p3_set_high = (p_3_high_raw - 500) * 0.1f;
    float p3_set_low  = (p_3_low_raw - 500) * 0.1f;

    float p4_temp     = (p_4_temp_raw - 500) * 0.1f;
    float p4_set_high = (p_4_high_raw - 500) * 0.1f;
    float p4_set_low  = (p_4_low_raw - 500) * 0.1f;

    /* clang-format off */
   data_t *data = data_make(
            "model",                "",                 DATA_STRING, "Maverick-XR50",
            "id",                   "",                 DATA_FORMAT, "%04x",    DATA_INT, id,
            "probe_1_flags",        "Flags Probe 1",    DATA_FORMAT, "%1x",     DATA_INT, p_1_flags,
            "temperature_1_C",      "Temperature 1",    DATA_COND, p_1_temp_raw != 0, DATA_FORMAT, "%.1f C", DATA_DOUBLE, p1_temp,
            "setpoint_high_1_C",    "Setpoint 1 high",  DATA_FORMAT, "%.1f C",  DATA_DOUBLE, p1_set_high,
            "setpoint_low_1_C",     "Setpoint 1 low",   DATA_FORMAT, "%.1f C",  DATA_DOUBLE, p1_set_low,
            "probe_2_flags",        "Flags Probe 2",    DATA_FORMAT, "%1x",     DATA_INT, p_2_flags,
            "temperature_2_C",      "Temperature 2",    DATA_COND, p_2_temp_raw != 0, DATA_FORMAT, "%.1f C", DATA_DOUBLE, p2_temp,
            "setpoint_high_2_C",    "Setpoint 2 high",  DATA_FORMAT, "%.1f C",  DATA_DOUBLE, p2_set_high,
            "setpoint_low_2_C",     "Setpoint 2 low",   DATA_FORMAT, "%.1f C",  DATA_DOUBLE, p2_set_low,
            "probe_3_flags",        "Flags Probe 3",    DATA_FORMAT, "%1x",     DATA_INT, p_3_flags,
            "temperature_3_C",      "Temperature 3",    DATA_COND, p_3_temp_raw != 0, DATA_FORMAT, "%.1f C", DATA_DOUBLE, p3_temp,
            "setpoint_high_3_C",    "Setpoint 3 high",  DATA_FORMAT, "%.1f C",  DATA_DOUBLE, p3_set_high,
            "setpoint_low_3_C",     "Setpoint 3 low",   DATA_FORMAT, "%.1f C",  DATA_DOUBLE, p3_set_low,
            "probe_4_flags",        "Flags Probe 4",    DATA_FORMAT, "%1x",     DATA_INT, p_4_flags,
            "temperature_4_C",      "Temperature 4",    DATA_COND, p_4_temp_raw != 0, DATA_FORMAT, "%.1f C", DATA_DOUBLE, p4_temp,
            "setpoint_high_4_C",    "Setpoint 4 high",  DATA_FORMAT, "%.1f C",  DATA_DOUBLE, p4_set_high,
            "setpoint_low_4_C",     "Setpoint 4 low",   DATA_FORMAT, "%.1f C",  DATA_DOUBLE, p4_set_low,
            "mic",                  "Integrity",        DATA_STRING, "CRC",
            NULL
    );
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "probe_1_flags",
        "temperature_1_C",
        "setpoint_high_1_C",
        "setpoint_low_1_C",
        "probe_2_flags",
        "temperature_2_C",
        "setpoint_high_2_C",
        "setpoint_low_2_C",
        "probe_3_flags",
        "temperature_3_C",
        "setpoint_high_3_C",
        "setpoint_low_3_C",
        "probe_4_flags",
        "temperature_4_C",
        "setpoint_high_4_C",
        "setpoint_low_4_C",
        "mic",
        NULL,
};

r_device const maverick_xr50 = {
        .name        = "Maverick XR-50 BBQ Sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 107,
        .long_width  = 107,
        .reset_limit = 2200,
        .decode_fn   = &maverick_xr50_decode,
        .fields      = output_fields,
};
