/** @file
    ThermoPro TP862b TempSpike XR 1,000-ft Wireless Dual-Probe Meat Thermometer.

    Copyright (C) 2026 n6ham <github.com/n6ham>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int thermopro_tp862b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
ThermoPro TP862b TempSpike XR 1,000-ft Wireless Dual-Probe Meat Thermometer.

Example data:

    rtl_433 % rtl_433 -f 915M -F json -X 'n=name,m=FSK_PCM,s=104,l=104,r=2000,preamble=d2552dd4,bits=170' | jq --unbuffered -r '.codes[0]'
    (spaces below added manually)

    {74}36 8a 2a1 2a5 1f 3f c738 0 [internal: 17.3C, ambient: 17.7C]
    {74}36 8a 2a1 2a5 1f 3f c738 0 [internal: 17.3C, ambient: 17.7C]
    {74}c5 9a 2a4 2a9 19 3f fa05 0 [internal: 17.6C, ambient: 18.1C]
    {74}c5 9a 2a5 2a9 19 3f 9d62 0 [internal: 17.7C, ambient: 18.1C]

Payload format:
- Preamble         {28} 0xd2552dd4
- Id               {8} Probe id (seems like it's unique for a probe and doesn't change)
- Probe            {8} Probe code (
    Black: 0x8a or 0xca when docked
    White: 0x9a or 0xda when docked
- Internal         {12} Raw internal temperature value (raw = temp_c * 10 + 500). Example: 17.3 C -> 0x2a1
- Ambient          {12} Raw ambient temperature value (raw = temp_c * 10 + 500). Example: 18.1 C -> 0x2a9
- Flags            {8}  A battery state, or something else.
- Separator        {8}  0x3f
- Checksum         {16} [CRC-8][~CRC-8]

Experimental data:
- Color            (Probe & 0x10) >> 4 (0 for black, 1 for white)
- Docked           (Probe & 0x40) >> 6 (0 for undocked, 1 for docked)

Data layout:
    ID:8h PROBE:8h INTERNAL:12d AMBIENT:12d FLAGS:8h SEPARATOR:8h CHECKSUM:16h T:8b
*/
static int thermopro_tp862b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xd2, 0x55, 0x2d, 0xd4};

    uint8_t b[9];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    int msg_len = bitbuffer->bits_per_row[0];
    if (msg_len < 170) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }
    if (msg_len > 170) {
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

    uint8_t id            = b[0];
    uint8_t probe         = b[1];
    uint16_t internal_raw = (b[2] << 4) | (b[3] >> 4);   // Internal: 12 bits starting at byte 2; Rounded to integer on the display
    uint16_t ambient_raw  = ((b[3] & 0x0f) << 8) | b[4]; // Ambient: 12 bits starting at the middle of byte 3; Rounded to integer on the display
    uint8_t flags         = b[5];

    float internal_c = (internal_raw - 500) * 0.1f;
    float ambient_c  = (ambient_raw - 500) * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",                "",                 DATA_STRING,    "ThermoPro-TP862b",
            "id",                   "",                 DATA_FORMAT,    "%02x",   DATA_INT,    id,
            "color",                "Color",            DATA_STRING,    (probe & 0x10) ? "white" : "black",
            "is_docked",            "Docked",           DATA_INT,       (probe & 0x40) >> 6,
            "temperature_int_C",    "Internal",         DATA_FORMAT,    "%.1f C", DATA_DOUBLE, internal_c,
            "temperature_amb_C",    "Ambient",          DATA_FORMAT,    "%.1f C", DATA_DOUBLE, ambient_c,
            "flags",                "Flags",            DATA_FORMAT,    "%02x",   DATA_INT,    flags,
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
        "flags",
        "mic",
        NULL,
};

r_device const thermopro_tp862b = {
        .name        = "ThermoPro TP862b TempSpike XR Wireless Dual-Probe Meat Thermometer",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 104,
        .long_width  = 104,
        .reset_limit = 2000,
        .decode_fn   = &thermopro_tp862b_decode,
        .fields      = output_fields,
};
