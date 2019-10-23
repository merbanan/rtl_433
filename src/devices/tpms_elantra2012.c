/* @file
    TPMS for Hyundai Elantra, Honda Civic.

    Copyright (C) 2019 Kumar Vivek <kv2000in@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
FSK 8 byte Manchester encoded TPMS with CRC8 checksum.
Seen on Hyundai Elantra, Honda Civic.

- TRW TPMS sensor FCC id GQ4-44T
- Mode/Sensor status: shipping, test, parking, driving, first block mode
- Battery voltage: Ok, low
- Trigger information: LF initiate TM
- Pressure: 1.4kPa
- temperature: 27 deg C
- acceleration: 0.5 g
- Market: EU, America
- Tire type: 450 kPa
- Response time: 8.14 seconds
- ID: 8 bytes

Preamble is 111 0001 0101 0101 (0x7155).
64 bits Manchester encoded data.

    PPTT IDID IDID FFCC

- P: Pressure in (8 bit), offset +60 = pressure in kPa
- T: Temperature (8 bit), offset -50 = temp in C
- I: ID (32 bit)
- F: Flags (8 bit) = ???? ?SBT (Missing Acceleration, market - Europe/US/Asia, Tire type, Alert Mode, park mode, High Line vs Low LIne etc)
  - S: Storage bit
  - B: Battery low bit
  - T: Triggered bit
  - C0 =1100 0000 = Battery OK, Not Triggered
  - C1 =1100 0001 = Battery OK, Triggered
  - C2 =1100 0010 = Battery Low, Not Triggered
  - C3 =1100 0011 = Battery Low, Triggered
  - C5 =1100 0101 = Battery OK, Triggered, Storage Mode
  - E1 =1110 0001 = Mx Sensor Clone for Elantra 2012 US market ? Low Line
  - C1		 = Mx Sensor Clone for Genesis Sedan 2012 US market ? High Line
- C: CRC-8, poly 0x07, init 0x00

*/

#include "decoder.h"

static int tpms_elantra2012_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    data_t *data;
    unsigned start_pos;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    uint32_t id;
    char id_str[9];
    int flags;
    char flags_str[3];
    int pressure_kpa;
    int temperature_c;
    int triggered, battery_low, storage;

    start_pos = bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 64);
    // require 64 data bits
    if (start_pos - bitpos < 128) {
        return DECODE_ABORT_LENGTH;
    }
    b = packet_bits.bb[0];

    if (crc8(b, 8, 0x07, 0x00)) {
        return DECODE_FAIL_MIC;
    }

    id = ((uint32_t)b[2] << 24) | (b[3] << 16) | (b[4] << 8) | (b[5]);
    sprintf(id_str, "%08x", id);

    flags = b[6];
    sprintf(flags_str, "%x", flags);

    pressure_kpa  = b[0] + 60;
    temperature_c = b[1] - 50;

    storage = (b[6] & 0x04) >> 2;
    battery_low = (b[6] & 0x02) >> 1;
    triggered = (b[6] & 0x01) >> 0;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Elantra2012",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (float)pressure_kpa,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C", DATA_DOUBLE, (float)temperature_c,
            "battery_ok",       "Battery",      DATA_INT,    !battery_low,
            "triggered",        "LF Triggered", DATA_INT,    triggered,
            "storage",          "Storage mode", DATA_INT,    storage,
            "flags",            "All Flags",    DATA_STRING, flags_str,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static int tpms_elantra2012_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Note that there is a (de)sync preamble of long/short, short/short, triple/triple,
    // i.e. 104 44, 52 48, 144 148 us pulse/gap.
    /* preamble = 111000101010101 0x71 0x55 */
    uint8_t const preamble_pattern[] = {0x71, 0x55}; // 16 bits

    int row;
    unsigned bitpos;
    int events = 0;

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                        (const uint8_t *)&preamble_pattern, 16)) + 128 <=
                bitbuffer->bits_per_row[row]) {
            int event = tpms_elantra2012_decode(decoder, bitbuffer, row, bitpos + 16);
            if (event > 0) {
                // not very clean, we ideally want the event to bubble up for accounting.
                // however, by adding them all together the accounting is wrong too.
                events += event;
            }
            bitpos += 15;
        }
    }

    return events;
}

static char *output_fields[] = {
        "model",
        "type",
        "id",
        "pressure_kPa",
        "temperature_C",
        "battery_ok",
        "triggered",
        "storage",
        "flags",
        NULL,
};

r_device tpms_elantra2012 = {
        .name        = "Elantra2012 TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 49,  // 12-13 samples @250k
        .long_width  = 49,  // FSK
        .reset_limit = 150, // Maximum gap size before End Of Message [us].
        .decode_fn   = &tpms_elantra2012_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
