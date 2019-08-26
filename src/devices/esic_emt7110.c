/** @file
    ESIC EMT7110 power meter (for EMR7370 receiver).

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>
    Samples and analysis by Petter Reinholdtsen <pere@hungry.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
ESIC EMT7110 power meter (for EMR7370 receiver).

- Center Frequency: 868.28 MHz
- Modulation: FSK
- Deviation: +/- 90 kHz
- Datarate: 9.579 kbit/s
- Preamble: 0xAAAA
- Sync-Word: 0x2DD4

A transmission is two packets, 14 ms apart.

Data Layout:

    II II II II FP PP CC CC VV UE EE XX

- I: (32 bit) byte 0-3: Sender ID
- F: (2 bit) byte 4 bit 7/6: Bit6 = power connected, Bit7 = Pairing mode
- P: (14 bit) byte 4 bit 5-0, byte 5: Power in 0.5 W
- C: (16 bit) byte 6-7: Current in mA
- V: (8 bit) byte 8: Voltage in V, Scaled by 2, Offset by 128 V
- U: (2 bit) byte 9 bit 7/6: unknown
- E: (14 bit) byte 9 bit 5-0, byte 10 Energyusage, total, in 10 Wh (0.01 kWh)
- X: (8 bit) byte 11: Sum of all 11 data bytes plus CHK is 0 (mod 256)

*/

#include "decoder.h"

static int esic_emt7110_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xAA, 0x2D, 0xD4};

    data_t *data;
    uint8_t b[12];

    if (bitbuffer->num_rows != 1)
        return DECODE_ABORT_EARLY;
    if (bitbuffer->bits_per_row[0] < 120)
        return DECODE_ABORT_LENGTH;

    unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof (preamble) * 8);
    offset += sizeof(preamble) * 8; // skip preamble
    if (offset > bitbuffer->bits_per_row[0])
        return DECODE_ABORT_EARLY;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 96);

    int chk = add_bytes(b, 12);
    if (chk & 0xff)
        return DECODE_FAIL_MIC;

    uint32_t id      = ((unsigned)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3]);
    int pairing      = (b[4] & 0x80) >> 7;
    int connected    = (b[4] & 0x40) >> 6;
    int power_raw    = ((b[4] & 0x3f) << 8) | (b[5]);
    float power_w    = power_raw * 0.5;
    int current_ma   = (b[6] << 8) | (b[7]);
    float current_a   = current_ma * 0.001;
    float voltage_v  = (b[8] + 256) * 0.5;
    int energy_raw   = ((b[9] & 0x3f) << 8) | (b[10]);
    float energy_kwh = energy_raw * 0.01;

    /* clang-format off */
    data = data_make(
            "model",        "",             DATA_STRING, "ESIC-EMT7110",
            "id",           "Sensor ID",    DATA_FORMAT, "%08x",     DATA_INT,    id,
            "power_W",      "Power",        DATA_FORMAT, "%.1f W",   DATA_DOUBLE, power_w,
            "current_A",    "Current",      DATA_FORMAT, "%.3f A",   DATA_DOUBLE, current_a,
            "voltage_V",    "Voltage",      DATA_FORMAT, "%.1f V",   DATA_DOUBLE, voltage_v,
            "energy_kWh",   "Energy",       DATA_FORMAT, "%.2f kWh", DATA_DOUBLE, energy_kwh,
            "pairing",      "Pairing?",     DATA_INT,    pairing,
            "connected",    "Connected?",   DATA_INT,    connected,
            "mic",          "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "power_W",
        "current_A",
        "voltage_V",
        "energy_kWh",
        "pairing",
        "connected",
        "mic",
        NULL,
};

r_device esic_emt7110 = {
        .name        = "ESIC EMT7110 power meter",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 104,
        .long_width  = 104,
        .reset_limit = 10000,
        .decode_fn   = &esic_emt7110_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
