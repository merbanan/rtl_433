/** @file
    LaCrosse TX22U IT+ temperature/humidity/wind/rain sensor.

    Copyright (C) 2024 rtl_433 contributors
    Based on protocol documentation by Mohammad Nikseresht and Goetz Romahn:
    http://nikseresht.com/blog/?p=99
    http://www.g-romahn.de/ws1600/Datepakete_raw.txt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
LaCrosse TX22U IT+ temperature, humidity, wind speed/direction and rain sensor.

Also known as Technoline TX22-IT, sold with WS-1600IT/WS-1610/WS-1611 weather stations.
This is the older IT+ protocol, distinct from the newer TX22U-IT (FSK-based) protocol.

Product pages:
https://www.lacrossetechnology.com/tx22/manual.pdf

Specifications:
- Frequency: 868 MHz (EU) or 915 MHz (NA) with frequency hopping (910/915/920 MHz)
- Bit rate: 8.621 kbps (~116 us per bit)
- Modulation: OOK PWM

Protocol Specification:

Data is organized in nibbles and transmitted using PWM (pulse width modulation)
with a fixed gap of ~108 us. Pulse width determines the nibble value:
- Pulse width / 116 us = nibble value (0-9)

Full message structure (nibble-organized):

    PRE:12h START:4h ID:6b ACQ:1b ERR:1b COUNT:4h Q1_TYPE:4h Q1_DATA:12d Q2_TYPE:4h Q2_DATA:12d ... CRC:16h

- PRE: preamble, typically 0x7ef (111111101111)
- START: always 0xa (1010)
- ID: 6-bit random transmitter identifier
- ACQ: acquisition phase flag (1 during first ~5 hours after power-on)
- ERR: error flag (set when wind sensor missing or battery low)
- COUNT: number of quartets following (1-5)
- Quartets: each is type (4 bits) + data (12 bits)
    Type 0: temperature, BCD coded tenths of C plus 400 (e.g. 0x628 -> 22.8 C)
    Type 1: humidity, BCD coded percent (e.g. 0x033 -> 33 %RH)
    Type 2: rain, counter of contact closures (scale ~0.518 mm per count)
    Type 3: wind, direction (first nibble x 22.5 degrees) + speed (next two nibbles in 0.1 m/s)
    Type 4: wind gust, speed in 0.1 m/s
- CRC: 16-bit checksum (last 2 bytes always 0x00 when valid)

After power-on, the transmitter sends full 5-quartet packets every ~4.5 seconds
for approximately 5 hours (acquisition phase). Thereafter, shorter packets
containing only 1-3 quartets are sent every 13-14 seconds.

Test messages (nikseresht.com, acquisition phase):

    a1250444109120003808401b89 00  -> Temp 044, Humi 91, Rain 000, Wind 028, Dir 180, Gust 097
                                       (4.4 C, 91% rH, no rain, wind 2.8 m/s from south, gust 9.7 m/s)
    a1250444109120003709401b41 00  -> Temp 044, Humi 91, Rain 000, Wind 032, Dir 157, Gust 097

Test messages (nikseresht.com, post-acquisition):

    a3832005380c401d59 00  -> Rain 025, Wind 043, Dir 180, Gust 104
    a3822005380fbe 00      -> Temp 042, Rain 025, Wind 068, Dir 157
    a38320053512400b66 00  -> Rain 025, Wind 064, Dir 112, Gust 039
    a383109120053712bc 00  -> Humi 91, Rain 025, Wind 064, Dir 157

Note: The test files in tests/lacrosse/tx22u/ are short captures (64ms) that only
contain partial messages. Full captures are needed for complete protocol decoding.
This decoder uses OOK_PWM with s=116, l=232 to decode the pulse widths.
*/

#include "decoder.h"

static int lacrosse_tx22u_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = { 0x7f, 0x7a };
    data_t *data;
    int row;
    uint8_t *b;
    int bit_offset;
    int nbits;
    int id;
    int flags;
    int count;
    int raw_temp = -1, humidity = -1, raw_speed = -1, direction = -1;
    int rain_count = -1;
    int wind_gust = -1;
    float temp_c, rain_mm, speed_ms, gust_ms;

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        nbits = bitbuffer->bits_per_row[row];

        if (nbits < 34)
            continue;

        bit_offset = bitbuffer_search(bitbuffer, row, 0,
                preamble_pattern, sizeof(preamble_pattern) * 8);

        if (bit_offset >= nbits)
            continue;

        b = bitbuffer->bb[row];
        b += bit_offset / 8;

        id = (b[2] >> 2) & 0x3f;
        flags = ((b[2] & 0x03) << 2) | (b[3] >> 6);
        count = (b[3] >> 2) & 0x0f;

        if (count < 1 || count > 5)
            continue;

        int pos = 4;
        int bits_remaining = nbits - bit_offset - 32;

        for (int i = 0; i < count && bits_remaining >= 16; ++i) {
            int type = (b[pos] >> 4) & 0x0f;
            int data_val = ((b[pos] & 0x0f) << 8) | b[pos + 1];
            pos += 2;
            bits_remaining -= 16;

            switch (type) {
            case 0:
                raw_temp = data_val;
                break;
            case 1:
                humidity = data_val;
                break;
            case 2:
                rain_count = data_val;
                break;
            case 3:
                direction = ((b[pos - 2] & 0x0f) >> 0) * 22.5;
                raw_speed = b[pos - 1];
                break;
            case 4:
                wind_gust = data_val;
                break;
            default:
                break;
            }
        }

        if (raw_temp != -1)
            temp_c = (raw_temp - 400) * 0.1f;
        else
            temp_c = -1;

        if (rain_count != -1)
            rain_mm = rain_count * 0.518f;
        else
            rain_mm = -1;

        speed_ms = raw_speed * 0.1f;
        gust_ms = wind_gust * 0.1f;

        /* clang-format off */
        data = data_make(
                "model",            "",               DATA_STRING, "LaCrosse-TX22U-IT+",
                "id",               "Sensor ID",      DATA_INT, id,
                "flags",            "Flags",          DATA_FORMAT, "%02x", DATA_INT, flags,
                "temperature_C",    "Temperature",    DATA_COND, raw_temp != -1,
                    DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                "humidity",         "Humidity",       DATA_COND, humidity != -1,
                    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                "rain_mm",          "Rain",           DATA_COND, rain_count != -1,
                    DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_mm,
                "wind_avg_ms",      "Wind Speed",     DATA_COND, raw_speed != -1,
                    DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, speed_ms,
                "wind_gust_ms",     "Wind Gust",      DATA_COND, wind_gust != -1,
                    DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, gust_ms,
                "wind_dir_deg",     "Wind Direction", DATA_COND, direction != -1,
                    DATA_INT, direction,
                "mic",              "Integrity",      DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return DECODE_ABORT_EARLY;
}

static char *output_fields[] = {
        "model",
        "id",
        "flags",
        "temperature_C",
        "humidity",
        "rain_mm",
        "wind_avg_ms",
        "wind_gust_ms",
        "wind_dir_deg",
        "mic",
        NULL,
};

r_device lacrosse_tx22u = {
        .name        = "LaCrosse TX22U IT+",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 116,
        .long_width  = 232,
        .reset_limit = 10000,
        .decode_fn   = &lacrosse_tx22u_decode,
        .fields      = output_fields,
};
