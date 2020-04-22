/** @file
    CurrentCost TX, CurrentCost EnviR current sensors.

    Copyright (C) 2015 Emmanuel Navarro <enavarro222@gmail.com>
    CurrentCost EnviR added by Neil Cowburn <git@neilcowburn.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

static int current_cost_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    bitbuffer_t packet = {0};
    uint8_t *b;
    int is_envir = 0;
    unsigned int start_pos;

    bitbuffer_invert(bitbuffer);

    uint8_t init_pattern_classic[] = {0xcc, 0xcc, 0xcc, 0xce, 0x91, 0x5d}; // 45 bits (! last 3 bits is not init)

    // The EnviR transmits 0x55 0x55 0x55 0x55 0x2D 0xD4
    // which is a 4-byte preamble and a 2-byte syncword
    // The init pattern is inverted and left-shifted by
    // 1 bit so that the decoder starts with a high bit.
    uint8_t init_pattern_envir[] = {0x55, 0x55, 0x55, 0x55, 0xa4, 0x57};

    start_pos = bitbuffer_search(bitbuffer, 0, 0, init_pattern_envir, 48);

    if (start_pos + 47 + 112 <= bitbuffer->bits_per_row[0]) {
        is_envir = 1;
        // bitbuffer_search matches patterns starting on a high bit, but the EnviR protocol
        // starts with a low bit, so we have to adjust the offset by 1 to prevent the
        // Manchester decoding from failing. This is perfectly safe though has the 47th bit
        // is always 0 as it's the last bit of the 0x2DD4 syncword, i.e. 0010110111010100.
        start_pos += 47;
    }
    else {
        start_pos = bitbuffer_search(bitbuffer, 0, 0, init_pattern_classic, 45);

        if (start_pos + 45 + 112 > bitbuffer->bits_per_row[0]) {
            return DECODE_ABORT_EARLY;
        }

        start_pos += 45;
    }

    start_pos = bitbuffer_manchester_decode(bitbuffer, 0, start_pos, &packet, 0);

    if (packet.bits_per_row[0] < 56) {
        return DECODE_ABORT_EARLY;
    }

    b = packet.bb[0];
    // Read data
    // Meter (b[0] = 0000xxxx) bits 5 and 4 are "unknown", but always 0 to date.
    if ((b[0] & 0xf0) == 0) {
        uint16_t device_id = (b[0] & 0x0f) << 8 | b[1];
        uint16_t watt0 = 0;
        uint16_t watt1 = 0;
        uint16_t watt2 = 0;
        //Check the "Data valid indicator" bit is 1 before using the sensor values
        if ((b[2] & 0x80) == 128)
            watt0 = (b[2] & 0x7F) << 8 | b[3];
        if ((b[4] & 0x80) == 128)
            watt1 = (b[4] & 0x7F) << 8 | b[5];
        if ((b[6] & 0x80) == 128)
            watt2 = (b[6] & 0x7F) << 8 | b[7];
        /* clang-format off */
        data = data_make(
                "model",         "",              DATA_STRING, is_envir ? "CurrentCost-EnviR" : _X("CurrentCost-TX","CurrentCost TX"), //TODO: it may have different CC Model ? any ref ?
                //"rc",            "Rolling Code",  DATA_INT, rc, //TODO: add rolling code b[1] ? test needed
                _X("id","dev_id"),       "Device Id",     DATA_FORMAT, "%d", DATA_INT, device_id,
                _X("power0_W","power0"),       "Power 0",       DATA_FORMAT, "%d W", DATA_INT, watt0,
                _X("power1_W","power1"),       "Power 1",       DATA_FORMAT, "%d W", DATA_INT, watt1,
                _X("power2_W","power2"),       "Power 2",       DATA_FORMAT, "%d W", DATA_INT, watt2,
                //"battery",       "Battery",       DATA_STRING, battery_low ? "LOW" : "OK", //TODO is there some low battery indicator ?
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    // Counter (b[0] = 0100xxxx) bits 5 and 4 are "unknown", but always 0 to date.
    else if ((b[0] & 0xf0) == 64) {
       uint16_t device_id = (b[0] & 0x0f) << 8 | b[1];
       // b[2] is "Apparently unused"
       uint16_t sensor_type = b[3]; //Sensor type. Valid values are: 2-Electric, 3-Gas, 4-Water
       uint32_t c_impulse = (unsigned)b[4] << 24 | b[5] <<16 | b[6] <<8 | b[7];
       /* clang-format off */
       data = data_make(
               "model",        "",              DATA_STRING, is_envir ? "CurrentCost-EnviRCounter" :_X("CurrentCost-Counter","CurrentCost Counter"), //TODO: it may have different CC Model ? any ref ?
               _X("subtype","sensor_type"),  "Sensor Id",     DATA_FORMAT, "%d", DATA_INT, sensor_type, //Could "friendly name" this?
               _X("id","dev_id"),       "Device Id",     DATA_FORMAT, "%d", DATA_INT, device_id,
               //"counter",      "Counter",       DATA_FORMAT, "%d", DATA_INT, c_impulse,
               "power0",       "Counter",       DATA_FORMAT, "%d", DATA_INT, c_impulse,
               NULL);
       /* clang-format on */
       decoder_output_data(decoder, data);
       return 1;
    }

    return 0;
}

static char *output_fields[] = {
        "model",
        "dev_id", // TODO: delete this
        "id",
        "sensor_type", // TODO: delete this
        "subtype",
        "power0", // TODO: delete this
        "power1", // TODO: delete this
        "power2", // TODO: delete this
        "power0_W",
        "power1_W",
        "power2_W",
        NULL,
};

r_device current_cost = {
        .name        = "CurrentCost Current Sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 250,
        .long_width  = 250, // NRZ
        .reset_limit = 8000,
        .decode_fn   = &current_cost_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
