/** @file
    AVE TPMS FSK 11 byte differential Manchester encoded CRC-8 TPMS data.

    Copyright (C) 2021 Pascal Charest

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
AVE TPMS FSK 11 byte differential Manchester encoded CRC-8 TPMS data.


Packet nibbles:

    PRE    IIIIIIII PP TT FF  CC

- PRE = preamble is 0xff 0xfe
- I = sensor Id in hex
- P = Pressure (4 conversion tables available)
- T = Temperature (deg C offset by 50)
- F = Flags
--    mode: 2 bits, mode 0 and 1 are 2.35kPa per pressure bit, mode 2 and 3 are 5.5kPa
--    battery: 3 bits, 7 is low, 6 not full and all other is full
--    unknown: 3 bits, last bit seems to swap from time to time
- C = CRC-8 with poly 0x31 init 0xff (alternatively, 0xd3 and 0x1e)
*/

#include "decoder.h"

static int tpms_ave_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    unsigned id;
    int mode;
    int pressure_raw;
    double pressure;
    int temperature;
    int battery_level;
    int flags;
    int crc;
    double ratio;
    double offset;

    bitbuffer_differential_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 160);

    if (packet_bits.bits_per_row[row] < 64) {
        return DECODE_ABORT_LENGTH; // too short to be a whole packet
    }
    decoder_log_bitbuffer(decoder, 1, __func__, &packet_bits, "");

    b = packet_bits.bb[row];

    id            = (unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    pressure_raw  = b[4];
    temperature   = b[5];
    mode          = b[6] >> 6 & 0x3;
    battery_level = b[6] >> 3 & 0x7;
    flags         = b[6] & 0x7;
    crc           = b[7];

    if (crc8(b, 7, 0x31, 0xff) != crc) {
        return DECODE_FAIL_MIC; // bad checksum
    }

    switch (mode) {
    case 0:
        ratio  = 2.352f;
        offset = 47.0f;
        break;
    case 1:
    default:
        ratio  = 2.352f;
        offset = 0.0f;
        break;
    case 2:
        ratio  = 5.491f;
        offset = 18.2f;
        break;
    case 3:
        ratio  = 5.491f;
        offset = 0.0f;
        break;
    }
    pressure = ((double)pressure_raw - offset) * ratio;

    char id_str[9 + 1];
    snprintf(id_str, sizeof(id_str), "%08x", id);

    /* clang-format off */
    data_t *data = NULL;
    data = data_str(data, "model",            "Model",         NULL,         "AVE");
    data = data_str(data, "type",             "Type",          NULL,         "TPMS");
    data = data_str(data, "id",               "Id",            NULL,         id_str);
    data = data_int(data, "mode",             "Mode",          "M%d",        mode);
    data = data_dbl(data, "pressure_kPa",     "Pressure",      "%.1f kPa",   pressure);
    data = data_dbl(data, "temperature_C",    "Temperature",   "%.0f C",     (double)temperature - 50.0);
    if (battery_level < 6) {
        data = data_dbl(data, "battery_ok",       "Battery level", NULL,         1.0);
    }
    if (battery_level == 6) {
        data = data_dbl(data, "battery_ok",       "Battery level", NULL,         0.75);
    }
    if (battery_level == 7) {
        data = data_dbl(data, "battery_ok",       "Battery level", NULL,         0.25);
    }
    data = data_int(data, "flags",            "Flags",         "0x%x",       flags);
    data = data_str(data, "mic",              "Integrity",     NULL,         "CRC");
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
Wrapper for the AVE tpms.
@sa tpms_ave_decode()
*/
static int tpms_ave_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xcc, 0xcc, 0xcc, 0xcd}; // Raw pattern, before differential Manchester coding

    int row;
    unsigned bitpos;
    int ret    = 0;
    int events = 0;

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 32)) + 132 <=
                bitbuffer->bits_per_row[0]) {
            ret = tpms_ave_decode(decoder, bitbuffer, row, bitpos + 32);
            if (ret > 0) {
                events += ret;
                bitpos += 132;
            }
            bitpos += 31;
        }
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "mode",
        "pressure_kPa",
        "temperature_C",
        "battery_level",
        "flags",
        "mic",
        NULL,
};

r_device const tpms_ave = {
        .name        = "AVE TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 400,
        .tolerance   = 15,
        .decode_fn   = &tpms_ave_callback,
        .fields      = output_fields,
};
