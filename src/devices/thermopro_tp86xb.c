/** @file
    ThermoPro TempSpike XR TP862b / TP863b Wireless Dual-Probe Meat Thermometer.

    Copyright (C) 2026 n6ham <github.com/n6ham>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"
#include <stdbool.h>

/** @fn int thermopro_tp86xb_decode(r_device *decoder, bitbuffer_t *bitbuffer)
ThermoPro TempSpike XR TP862b / TP863b Wireless Dual-Probe Meat Thermometer.

Example data:

    rtl_433 % rtl_433 -f 915M -F json -X 'n=ThermoPro-TempSpikeXR,m=FSK_PCM,s=104,l=104,r=2000,preamble=d2552dd4,bits=165' | jq --unbuffered -r '.codes[0]'

        {74}9c9a2bc2c50b1fa8570
        {77}9c9a2bc2c5cb116f0000
        {74}9c9a2bc2c50b1fa8570
        {77}9c9a2bc2c5cb116f0000

Data layout:
        ID:8d 1x IS_DOCKED:1b 1x COLOR:1b 4x INT:12d AMB:12d IS_BOOSTER:2b ?:6 ?:2b PROBE_BAT:2d IS_PROBE:2b BOOSTER_BAT:2d CHK:16h
Byte:   0     1                              2               5                 6                                            7 - 8

Payload format:
- Preamble         {28} 0xd2552dd4
- Id               {8} Probe id (seems like it's unique for a probe and doesn't change)
- ?                {1}
- Docked           {1}
- ?                {1}
- Color            {1}
- ?                {4}
- Internal         {12} Raw internal temperature value (raw = temp_c * 10 + 500). Example: 17.3 C -> 0x2a1
- Ambient          {12} Raw ambient temperature value (raw = temp_c * 10 + 500). Example: 18.1 C -> 0x2a9
- Is booster       {2} 0x3 for booster, 0 for probe
- ?                {8}
- Probe batery     {2} full - 3, empty - 0 (number of the battery indicator bars)
- Is probe         {2} 0x3 for probe, 0 for booster (inverse of 'Is booster')
- Booster batery   {2} full - 3, empty - 0 (number of the battery indicator bars)
- Checksum         {16} [CRC-8][~CRC-8]

*/
static int thermopro_tp86xb_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xd2, 0x55, 0x2d, 0xd4};

    uint8_t b[9];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    int msg_len = bitbuffer->bits_per_row[0];
    if (msg_len < 165) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }
    if (msg_len > 173) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    int offset = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= msg_len) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 9 * 8);

    // Validate Checksum format. Byte 7 must be equal to byte 8 inverted.
    if (b[7] == ~b[8]) {
        decoder_logf(decoder, 2, __func__, "Checksum byte 7 is supposed to be equal to byte 8 inverted. Actual: %02x vs %02x (inverted %02x)", b[7], b[8], ~b[8]);
        return DECODE_FAIL_MIC;
    }

    // Validate Checksum: CRC-8 (Poly 0x07, Init 0x00, Final XOR 0xDB. The checksum is stored as [CRC-8][~CRC-8] in bytes 7 and 8
    uint8_t calc_crc = crc8(b, 7, 0x07, 0x00) ^ 0xdb;
    if (calc_crc != b[7]) {
        decoder_logf(decoder, 2, __func__, "Integrity check failed %02x vs %02x", b[7], calc_crc);
        return DECODE_FAIL_MIC;
    }

    uint8_t  id              = b[0];
    int8_t   is_white        = (b[1] & 0x10) >> 4;
    uint8_t  is_docked       = (b[1] & 0x40) >> 6;
    uint16_t internal_raw    = (b[2] << 4) | (b[3] >> 4);   // Internal: 12 bits starting at byte 2; Rounded to integer on the display
    uint16_t ambient_raw     = ((b[3] & 0x0f) << 8) | b[4]; // Ambient: 12 bits starting at the middle of byte 3; Rounded to integer on the display
    uint8_t  is_probe        = (b[6] & 0x0c) == 0x0c;
    uint8_t  is_booster      = (b[5] & 0xc0) == 0xc0;
    uint8_t  probe_battery   = (b[6] & 0x30) >> 4;
    uint8_t  booster_battery = (b[6] & 0x03);


    float internal_c = (internal_raw - 500) * 0.1f;
    float ambient_c  = (ambient_raw - 500) * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",                "",                 DATA_STRING,    "ThermoPro-TempSpikeXR",
            "id",                   "",                 DATA_FORMAT,    "%02x",   DATA_INT,    id,
            "color",                "Color",            DATA_STRING,    is_white ? "white" : "black",
            "is_docked",            "Is Docked",        DATA_COND,      is_docked, DATA_INT, is_docked,
            "temperature_int_C",    "Internal",         DATA_FORMAT,    "%.1f C", DATA_DOUBLE, internal_c,
            "temperature_amb_C",    "Ambient",          DATA_FORMAT,    "%.1f C", DATA_DOUBLE, ambient_c,
            "is_probe",             "Is Probe",         DATA_COND,      is_probe, DATA_INT, is_probe,
            "is_booster",           "Is Booster",       DATA_COND,      is_booster, DATA_INT, is_booster,
            "probe_batery",         "Probe Battery",    DATA_COND,      is_probe, DATA_INT, probe_battery,
            "booster_battery",      "Booster Battery",  DATA_COND,      is_booster, DATA_INT, booster_battery,
            "mic",                  "Integrity",        DATA_STRING,    "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "color",
        "is_docked",
        "temperature_int_C",
        "temperature_amb_C",
        "is_probe",
        "is_booster",
        "probe_batery",
        "booster_battery",
        "mic",
        NULL,
};

r_device const thermopro_tp86xb = {
        .name        = "ThermoPro TempSpike XR TP862b / TP863b Wireless Dual-Probe Meat Thermometer",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 104,
        .long_width  = 104,
        .reset_limit = 2000,
        .decode_fn   = &thermopro_tp86xb_decode,
        .fields      = output_fields,
};
