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

/**
CurrentCost TX, CurrentCost EnviR current sensors.

@todo Documentation needed.
*/
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

    bitbuffer_manchester_decode(bitbuffer, 0, start_pos, &packet, 0);

    if (packet.bits_per_row[0] < 64) {
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
        data = NULL;
        data = data_str(data, "model",        "",              NULL,         is_envir ? "CurrentCost-EnviR" : "CurrentCost-TX"); //TODO: it may have different CC Model ? any ref ?
                //"rc",           "Rolling Code",  DATA_INT, rc, //TODO: add rolling code b[1] ? test needed
        data = data_int(data, "id",           "Device Id",     "%d",         device_id);
        data = data_int(data, "power0_W",     "Power 0",       "%d W",       watt0);
        data = data_int(data, "power1_W",     "Power 1",       "%d W",       watt1);
        data = data_int(data, "power2_W",     "Power 2",       "%d W",       watt2);
                //"battery_ok",   "Battery",       DATA_INT,    !battery_low, //TODO is there some low battery indicator ?
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    // Counter (b[0] = 0100xxxx) bits 5 and 4 are "unknown", but always 0 to date.
    else if ((b[0] & 0xf0) == 64) {
        uint16_t device_id = (b[0] & 0x0f) << 8 | b[1];
        // b[2] is "Apparently unused"
        uint16_t sensor_type = b[3]; // Sensor type. Valid values are: 2-Electric, 3-Gas, 4-Water
        uint32_t c_impulse   = (unsigned)b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7];
        /* clang-format off */
        data = NULL;
        data = data_str(data, "model",         "",              NULL,         is_envir ? "CurrentCost-EnviRCounter" : "CurrentCost-Counter"); //TODO: it may have different CC Model ? any ref ?
        data = data_int(data, "subtype",       "Sensor Id",     "%d",         sensor_type); //Could "friendly name" this?
        data = data_int(data, "id",            "Device Id",     "%d",         device_id);
               //"counter",       "Counter",       DATA_FORMAT, "%d", DATA_INT, c_impulse,
        data = data_int(data, "power0",        "Counter",       "%d",         c_impulse);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return 0;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "subtype",
        "power0_W",
        "power1_W",
        "power2_W",
        "power0",
        NULL,
};

r_device const current_cost = {
        .name        = "CurrentCost Current Sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 250,
        .long_width  = 250, // NRZ
        .reset_limit = 8000,
        .decode_fn   = &current_cost_decode,
        .fields      = output_fields,
};
