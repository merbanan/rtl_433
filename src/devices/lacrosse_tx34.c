/** @file
    LaCrosse TX34-IT rain gauge decoder.

    Copyright (C) 2021 Reynald Poittevin <reynald@poittevin.name>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
LaCrosse TX34-IT rain gauge.

Can be bought here: https://en.lacrossetechnology.fr/P-20-A1-WSTX34IT.html

This sensor sends a frame every 6.5 s.

The LaCrosse "IT+" family share some specifications:
- Frequency: 868.3 MHz
- Modulation: FSK/PCM
- Bit duration: 58 µs
- Frame size: 64 bits (including preamble)

Frame format:

-------------
| 1010 1010 | preamble (some bits may be lost)
-------------
| 0010 1101 | 0x2dd4: sync word
| 1101 0100 |
-------------
| MMMM DDDD | MMMM: sensor model (5 for rain gauge, 9 for thermo/hydro...)
| DDNW 0000 | DDDDDD: device ID (0 to 63, random at startup)
| GGGG GGGG | N: new battery (on for about 420 minutes after startup)
| GGGG GGGG | W: weak battery (on when battery voltage < 2 volts)
------------- GGGGGGGGGGGGGGGG: bucket tipping counter
| CCCC CCCC | CCCCCCCC: CRC8 (poly 0x31 init 0x00) on previous 4 bytes
-------------

This decoder decodes generic LaCrosse IT+ frames and filters TX34 ones.
Could be merged with existing TX29 decoder... or not.
*/

#include "decoder.h"

#define LACROSSE_TX34_ITMODEL 5
#define LACROSSE_TX34_PAYLOAD_BITS 40
#define LACROSSE_TX34_RAIN_FACTOR 0.222f

static int lacrosse_tx34_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // 20 bits preamble (shifted left): 1010b 0x2DD4
    uint8_t const preamble[] = {0xa2, 0xdd, 0x40};

    // process all rows
    int events = 0;
    for (int row = 0; row < bitbuffer->num_rows; ++row) {

        // search for preamble
        unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, 20) + 20;
        if (start_pos + LACROSSE_TX34_PAYLOAD_BITS > bitbuffer->bits_per_row[row])
            continue; // preamble not found
        decoder_log(decoder, 2, __func__, "LaCrosse IT frame detected");
        // get payload
        uint8_t b[5];
        bitbuffer_extract_bytes(bitbuffer, row, start_pos, b, LACROSSE_TX34_PAYLOAD_BITS);
        // verify CRC
        int r_crc = b[4];
        int c_crc = crc8(b, 4, 0x31, 0x00);
        if (r_crc != c_crc) {
            // bad CRC: reject IT frame
            decoder_logf(decoder, 1, __func__, "LaCrosse IT frame bad CRC: calculated %02x, received %02x", c_crc, r_crc);
            continue;
        }

        // check model
        if (((b[0] & 0xF0) >> 4) != LACROSSE_TX34_ITMODEL)
            continue; // not a rain gauge...

        // decode payload
        int sensor_id = ((b[0] & 0x0F) << 2) | (b[1] >> 6);
        int new_batt  = (b[1] & 0x20) >> 5;
        int low_batt  = (b[1] & 0x10) >> 4;
        int rain_tick = (b[2] << 8) | b[3];
        float rain_mm = rain_tick * LACROSSE_TX34_RAIN_FACTOR;

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, "LaCrosse-TX34IT",
                "id",           "",             DATA_INT,    sensor_id,
                "battery_ok",   "Battery",      DATA_INT,    !low_batt,
                "newbattery",   "New battery",  DATA_INT,    new_batt,
                "rain_mm",      "Total rain",   DATA_DOUBLE, rain_mm,
                "rain_raw",     "Raw rain",     DATA_INT,    rain_tick,
                "mic",          "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        events++;
    }
    return events;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "newbattery",
        "rain_mm",
        "rain_raw",
        "mic",
        NULL,
};

r_device const lacrosse_tx34 = {
        .name        = "LaCrosse TX34-IT rain gauge",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 4000,
        .decode_fn   = &lacrosse_tx34_callback,
        .fields      = output_fields,
};
