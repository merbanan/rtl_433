/** @file
    LaCrosse TX34-IT rain gauge decoder.

    Copyright (C) 2021 Reynald Poittevin

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
| 1010 1010 | preamble (up to the first two bits may be lost)
-------------
| 0010 1101 | 0x2DD4: LaCrosse IT frame identifier
| 1101 0100 |
-------------
| MMMM DDDD | MMMM: sensor model (5 for rain gauge, 9 for thermo/hydro...)
| DDNW 0000 | DDDDDD: device ID (0 to 63, random at startup)
| GGGG GGGG | N: new battery (on for about 420 minutes after startup)
| GGGG GGGG | W: weak battery (on when battery voltage < 2 volts)
------------- GGGGGGGGGGGGGGGG: bucket tipping counter
| CCCC CCCC | CCCCCCCC: CRC8 on previous 4 bytes
-------------

This decoder decodes generic LaCrosse IT+ frames and filters TX34 ones.
Could be merged with existing TX29 decoder... or not.
*/

#include "decoder.h"

#define LACROSSE_TX34_CRC_POLY 0x31
#define LACROSSE_TX34_CRC_INIT 0x00
#define LACROSSE_TX34_PREAMBLE_BITS 22
#define LACROSSE_TX34_ITMODEL 5
#define LACROSSE_TX34_PAYLOAD_BITS 40
#define LACROSSE_TX34_RAIN_FACTOR 0.222

static int lacrosse_tx34_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int row;
    uint8_t payload[5];
    uint8_t r_crc, c_crc;
    uint8_t sensor_id, new_bat, weak_bat;
    uint16_t rain_tick;
    float rain_mm;
    int events;

    // 22 bits preamble (shifted left): 101010b 0x2DD4
    static const uint8_t preamble[] = { 0xA8, 0xB7, 0x50 };

    // process rows
    events = 0;
    for (row = 0; row <= bitbuffer->num_rows; ++row)
    {
        unsigned int start_pos, payload_bits;

        // search for preamble
        start_pos = bitbuffer_search(bitbuffer, row, 0, preamble,
            LACROSSE_TX34_PREAMBLE_BITS);
        if (start_pos == bitbuffer->bits_per_row[row])
        continue; // preamble not found
        payload_bits = bitbuffer->bits_per_row[row] - start_pos -
        LACROSSE_TX34_PREAMBLE_BITS;
        if (payload_bits < LACROSSE_TX34_PAYLOAD_BITS)
        continue; // probably truncated frame
        if (decoder->verbose)
        fprintf(stderr,
            "LaCrosse IT frame detected (%d bits payload)\n",
            payload_bits);

        // get payload
        bitbuffer_extract_bytes(bitbuffer, row,
            start_pos + LACROSSE_TX34_PREAMBLE_BITS,
            payload, LACROSSE_TX34_PAYLOAD_BITS);
        // verify CRC
        r_crc = payload[4];
        c_crc = crc8(&payload[0], 4,
            LACROSSE_TX34_CRC_POLY, LACROSSE_TX34_CRC_INIT);
        if (r_crc != c_crc)
        {
            // bad CRC: reject IT frame
            fprintf(stderr,
                "LaCrosse IT frame bad CRC: calculated %02x, "
                "received %02x\n",
                c_crc, r_crc);
            continue;
        }

        // check model
        if (((payload[0] & 0xF0) >> 4) != LACROSSE_TX34_ITMODEL)
            continue; // not a rain gauge...
        if (decoder->verbose)
            fprintf(stderr, "LaCrosse TX34-IT detected\n");

        // decode payload
        sensor_id = ((payload[0] & 0x0F) << 2) + (payload[1] >> 6);
        new_bat = (payload[1] & 0x20) >> 5;
        weak_bat = (payload[1] && 0x10) >> 4;
        rain_tick = (payload[2] << 8 ) + payload[3];
        rain_mm = rain_tick * LACROSSE_TX34_RAIN_FACTOR;
        /* clang-format off */
        data = data_make(
            "model",      "",            DATA_STRING, "LaCrosse TX34-IT",
            "id",         "",            DATA_INT,    sensor_id,
            "battery_ok", "Battery",     DATA_INT,    1 - weak_bat,
            "newbattery", "New battery", DATA_INT,    new_bat,
            "rain_mm",    "Total rain",  DATA_DOUBLE, rain_mm,
            "rain_raw",   "Raw rain",    DATA_INT,    rain_tick,
            "mic",        "Integrity",   DATA_STRING, "CRC",
            NULL
        );
        /* clang-format on */
        decoder_output_data(decoder, data);
        events++;
    }
    return events;
}

static char *output_fields[] =
{
    "model",
    "id",
    "battery_ok",
    "newbattery",
    "rain_mm",
    "rain_raw",
    "mic",
    NULL,
};

r_device lacrosse_tx34 =
{
    .name           = "LaCrosse TX34-IT rain gauge",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 58,
    .long_width     = 58,
    .reset_limit    = 4000,
    .decode_fn      = &lacrosse_tx34_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
