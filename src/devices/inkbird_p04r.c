/** @file
    Decoder for Inkbird IBS-P04R Pool Sensor.

    Copyright (C) 2026 Anthony Grieco

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Decoder for Inkbird IBS-P04R pool water quality sensor.

The sensor transmits temperature and TDS (total dissolved solids) on 433.92 MHz.
The device uses FSK-PCM encoding.
The device sends a transmission every ~80 sec.

Related to the Inkbird ITH-20R (protocol 194) but uses a different subtype,
CRC parameters, payload length, and field layout.

- Preamble: aa aa aa ... aa aa (1200+ bits of alternating 10101010)
- Sync Word (16 bits): 2DD4

Data layout:

    SS SS LL FF CC VV VV BB II II XX XX XX XX TT TT DD RR RR UU UU KK KK

- S: 16 bit Inkbird device family header (0xD391)
- L: 8 bit subtype/length (0x14 = 20 bytes following)
- F: 8 bit status flags (0x00 normal, 0x40 unlink, 0x80 battery replaced)
- C: 8 bit sensor configuration
- V: 16 bit firmware version
- B: 8 bit battery level (0-100)
- I: 16 bit device ID (little-endian)
- X: 32 bit per-device constant (secondary ID)
- T: 16 bit temperature, Celsius * 10 (little-endian, signed)
- D: 8 bit TDS (total dissolved solids) in ppm
- R: 16 bit reserved (always zero)
- U: 16 bit unknown (byte 19 constant, byte 20 toggles 0/1)
- K: 16 bit CRC-16/MODBUS over bytes 3-20 (little-endian)

CRC16 (bytes 3-20, excludes header):
poly=0x8005  init=0xFFFF  refin=true  refout=true  (CRC-16/MODBUS)

Note: The display unit computes EC (electrical conductivity) from TDS:
  EC (uS/cm) = TDS (ppm) * 20 / 11  (integer division, ~factor 0.55)
*/

#include "decoder.h"

#define INKBIRD_P04R_CRC_POLY 0xA001  // reflected 0x8005
#define INKBIRD_P04R_CRC_INIT 0xFFFF  // CRC-16/MODBUS
#define INKBIRD_P04R_MSG_LEN  23

static int inkbird_p04r_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0xaa, 0x2d, 0xd4};

    uint8_t msg[INKBIRD_P04R_MSG_LEN];

    if (bitbuffer->num_rows != 1
            || bitbuffer->bits_per_row[0] < 187) {
        decoder_logf(decoder, 2, __func__, "bit_per_row %u out of range", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH;
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof (preamble_pattern) * 8);

    if (start_pos == bitbuffer->bits_per_row[0]) {
        return DECODE_FAIL_SANITY;
    }

    start_pos += sizeof (preamble_pattern) * 8;
    unsigned len = bitbuffer->bits_per_row[0] - start_pos;

    decoder_logf(decoder, 2, __func__, "start_pos=%u len=%u", start_pos, len);

    if (((len + 7) / 8) < INKBIRD_P04R_MSG_LEN) {
        decoder_logf(decoder, 1, __func__, "%u too short", len);
        return DECODE_ABORT_LENGTH;
    }

    unsigned extract_len = MIN(len, sizeof (msg) * 8);
    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, extract_len);

    // Verify header D391 and subtype 0x14
    if (msg[0] != 0xD3 || msg[1] != 0x91 || msg[2] != 0x14) {
        decoder_logf(decoder, 1, __func__, "bad header %02X%02X%02X", msg[0], msg[1], msg[2]);
        return DECODE_FAIL_SANITY;
    }

    // CRC-16/MODBUS over bytes 3-20, stored LE at bytes 21-22
    uint16_t crc_calculated = crc16lsb(msg + 3, 18, INKBIRD_P04R_CRC_POLY, INKBIRD_P04R_CRC_INIT);
    uint16_t crc_received   = msg[22] << 8 | msg[21];

    decoder_logf(decoder, 2, __func__, "CRC 0x%04X = 0x%04X", crc_calculated, crc_received);

    if (crc_received != crc_calculated) {
        decoder_logf(decoder, 1, __func__, "CRC check failed (0x%04X != 0x%04X)", crc_calculated, crc_received);
        return DECODE_FAIL_MIC;
    }

    float battery_ok   = msg[7] * 0.01f;
    uint16_t sensor_id = (msg[9] << 8 | msg[8]);
    float temperature  = ((int16_t)(msg[15] << 8 | msg[14])) * 0.1f;
    int tds_ppm        = msg[16];

    decoder_logf(decoder, 1, __func__,
            "status=0x%02X sensor_cfg=0x%02X id_ext=%02X%02X%02X%02X unk19=0x%02X unk20=0x%02X",
            msg[3], msg[4], msg[10], msg[11], msg[12], msg[13], msg[19], msg[20]);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Inkbird-IBSP04R",
            "id",               "",                 DATA_INT,    sensor_id,
            "battery_ok",       "Battery level",    DATA_DOUBLE, battery_ok,
            "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
            "tds_ppm",          "TDS",              DATA_FORMAT, "%d ppm", DATA_INT,    tds_ppm,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "tds_ppm",
        "mic",
        NULL,
};

r_device const inkbird_p04r = {
        .name        = "Inkbird IBS-P04R pool sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 4000,
        .decode_fn   = &inkbird_p04r_decode,
        .fields      = output_fields,
};
