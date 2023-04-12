/** @file
    Fine Offset Electronics WN34 Temperature Sensor.

    Copyright (C) 2022 @anthyz

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Fine Offset Electronics WN34 Temperature Sensor.

- also Ecowitt WN34S (soil), WN34L (water)
- also Froggit DP150 (soil), DP35 (water)

Preamble is aaaa aaaa, sync word is 2dd4.

Packet layout:

     0  1  2  3  4  5  6  7  8
    YY II II II 0T TT BB XX AA
    34 00 29 65 02 85 44 66 f3

- Y: 8 bit fixed sensor type 0x34
- I: 24 bit device ID
- T: 11 bit temperature, offset 40, scale 10
- B: 7 bit battery level (unit of 20 mV)
- X: 8 bit CRC
- A: 8 bit checksum

*/

static int fineoffset_wn34_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t const preamble[] = {0xAA, 0x2D, 0xD4};
    uint8_t b[9];
    unsigned bit_offset;

    bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8;
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "short package. Row length: %u. Header index: %u", bitbuffer->bits_per_row[0], bit_offset);
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    decoder_log_bitrow(decoder, 1, __func__, b, sizeof (b) * 8, "");

    // Verify family code
    if (b[0] != 0x34) {
        decoder_logf(decoder, 2, __func__, "Msg family unknown: %2x", b[0]);
        decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "Row length(bits_per_row[0]): %u", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_EARLY;
    }

    // Verify checksum, same as other FO Stations: Reverse 1Wire CRC (poly 0x131)
    uint8_t crc = crc8(b, 7, 0x31, 0x00);
    uint8_t chk = add_bytes(b, 8);

    if (crc != b[7] || chk != b[8]) {
        decoder_logf(decoder, 2, __func__, "Checksum error: %02x %02x", crc, chk);
        return DECODE_FAIL_MIC;
    }

    // Decode data
    int id            = (b[1] << 16) | (b[2] << 8) | (b[3]);
    int temp_raw      = (b[4] & 0x7) << 8 | b[5];
    float temperature = (temp_raw - 400) * 0.1f;    // range -40.0-60.0 C
    int battery_mv    = (b[6] & 0x7f) * 20;         // mV

    /*
     * A 5 bar battery indicator is shown in the Ecowitt WS View app.
     * Through observation of battery_mv values and the app indicator,
     * it was determined that battery_mv maps non-linearly to the number
     * of bars. Set battery_ok by mapping battery bars from 0 to 1 where
     * 1 bar = 0 and 5 bars = 1.
     */
    int battery_bars;

    if (battery_mv > 1440)
        battery_bars = 5;
    else if (battery_mv > 1380)
        battery_bars = 4;
    else if (battery_mv > 1300)
        battery_bars = 3;
    else if (battery_mv > 1200)
        battery_bars = 2;
    else
        battery_bars = 1;

    float battery_ok  = (battery_bars - 1) * 0.25f;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Fineoffset-WN34",
            "id",               "ID",           DATA_FORMAT, "%x", DATA_INT, id,
            "battery_ok",       "Battery",      DATA_FORMAT, "%.1f", DATA_DOUBLE, battery_ok,
            "battery_mV",       "Battery Voltage", DATA_FORMAT, "%d mV", DATA_INT, battery_mv,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "battery_mV",
        "temperature_C",
        "mic",
        NULL,
};

r_device const fineoffset_wn34 = {
        .name        = "Fine Offset Electronics WN34 temperature sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 2500,
        .decode_fn   = &fineoffset_wn34_decode,
        .fields      = output_fields,
};
