/** @file
    Fine Offset Electronics WN34 Temperature Sensor.

    Copyright (C) 2022 \@anthyz

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Fine Offset Electronics WN34 Temperature Sensor.

- also Ecowitt WN34S (soil), WN34L (water), range is -40~60 °C (-40~140 °F)
- also Ecowitt WN34D (water), range is -55~125 °C (-67~257 °F)
- also Froggit DP150 (soil), DP35 (water)

Preamble is aaaa aaaa, sync word is 2dd4.

Packet layout:

     0  1  2  3  4  5  6  7  8  9 10
    YY II II II ST TT BB XX AA ZZ ZZ
    34 00 29 65 02 85 44 66 f3 20 10

- Y:{8}  fixed sensor type 0x34
- I:{24} device ID
- S:{4}  sub type, 0 = WN34L, 0x4 = WN34D
- T:{12} temperature, offset 40 (except WN34D), scale 10
- B:{7}  battery level (unit of 20 mV)
- X:{8}  bit CRC
- A:{8}  bit checksum
- Z:{13} trail byte, not used.

*/

static int fineoffset_wn34_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t const preamble[] = {0xAA, 0x2D, 0xD4};
    uint8_t b[9];
    unsigned bit_offset;
    float temperature;

    bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8;
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "short package. Row length: %u. Header index: %u", bitbuffer->bits_per_row[0], bit_offset);
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    decoder_log_bitrow(decoder, 1, __func__, b, sizeof (b) * 8, "");

    // Verify family code
    if (b[0] != 0x34) {
        decoder_logf(decoder, 2, __func__, "Msg family unknown: %02x", b[0]);
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
    int id          = (b[1] << 16) | (b[2] << 8) | (b[3]);
    int temp_raw    = (int16_t)((b[4] & 0x0F) << 12 | b[5] << 4); // use sign extend
    int sub_type    = (b[4] & 0xF0) >> 4;
    decoder_logf(decoder, 1, __func__, "subtype : %d", sub_type);

    if (sub_type == 4) // WN34D
        temperature = (temp_raw >> 4) * 0.1f;    // scale by 10 only.
    else // WN34L/WN34S ...
        temperature = ((temp_raw >> 4) * 0.1f) - 40;    // scale by 10, offset 40

    int battery_mv  = (b[6] & 0x7f) * 20;         // mV

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
            "model",         "",                DATA_COND, sub_type != 4, DATA_STRING, "Fineoffset-WN34",
            "model",         "",                DATA_COND, sub_type == 4, DATA_STRING, "Fineoffset-WN34D",
            "id",            "ID",              DATA_FORMAT, "%x",     DATA_INT,    id,
            "battery_ok",    "Battery",         DATA_FORMAT, "%.1f",   DATA_DOUBLE, battery_ok,
            "battery_mV",    "Battery Voltage", DATA_FORMAT, "%d mV",  DATA_INT,    battery_mv,
            "temperature_C", "Temperature",     DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
            "mic",           "Integrity",       DATA_STRING, "CRC",
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
        .name        = "Fine Offset Electronics WN34S/L/D and Froggit DP150/D35 temperature sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 2500,
        .decode_fn   = &fineoffset_wn34_decode,
        .fields      = output_fields,
};
