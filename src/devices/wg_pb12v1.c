/* WG-PB12V1 Temperature Sensor
 * ---
 * Device method to decode a generic wireless temperature probe. Probe marked
 * with WG-PB12V1-2016/11.
 *
 * Format of Packets
 * ---
 * The packet format appears to be similar those the Lacrosse format.
 * (http://fredboboss.free.fr/articles/tx29.php)
 *
 * AAAAAAAA ????TTTT TTTTTTTT ???IIIII HHHHHHHH CCCCCCCC
 *
 * A = Preamble - 11111111
 * ? = Unknown - possibly battery charge
 * T = Temperature - see below
 * I = ID of probe is set randomly each time the device is powered off-on,
 *     Note, base station has and unused "123" symbol, but ID values can be
 *     higher than this.
 * H = Humidity - not used, is always 11111111
 * C = Checksum - CRC8, polynomial 0x31, initial value 0x0, final value 0x0
 *
 * Temperature
 * ---
 * Temperature value is "milli-celsius", ie 1000 mC = 1C, offset by -40 C.
 *
 * 0010 01011101 = 605 mC => 60.5 C
 * Remove offset => 60.5 C - 40 C = 20.5 C
 *
 * Unknown
 * ---
 * Possible uses could be weak battery, or new battery.
 *
 * At the moment it this device cannot distinguish between a Fine Offset
 * device, see fineoffset.c.
 *
 * Copyright (C) 2015 Tommy Vestermark
 * Modifications Copyright (C) 2017 Ciarán Mooney
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

static int wg_pb12v1_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;

    uint8_t id;
    int16_t temp;
    float temperature;

    const uint8_t polynomial = 0x31;    // x8 + x5 + x4 + 1 (x8 is implicit)

    // Validate package
    b = bitbuffer->bb[0];
    if (bitbuffer->bits_per_row[0] < 48 ||               // Don't waste time on a short packages
            b[0] != 0xFF ||                              // Preamble
            b[5] != crc8(&b[1], 4, polynomial, 0) || // CRC (excluding preamble)
            b[4] != 0xFF)                                // Humidity set to 11111111
        return 0;

    // Nibble 7,8 contains id
    id = b[3] & 0x1F;

    // Nibble 5,6,7 contains 12 bits of temperature
    // The temperature is "milli-celsius", ie 1000 mC = 1C, offset by -40 C.
    temp = ((b[1] & 0x0F) << 8) | b[2];
    temperature = ((float)temp / 10) - 40;

    data = data_make(
            "model",            "",             DATA_STRING, "WG-PB12V1",
            "id",               "ID",           DATA_INT, id,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "temperature_C",
    "mic",
    NULL
};

r_device wg_pb12v1 = {
    .name           = "WG-PB12V1 Temperature Sensor",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 564, // Short pulse 564µs, long pulse 1476µs, fixed gap 960µs
    .long_width     = 1476, // Maximum pulse period (long pulse + fixed gap)
    .reset_limit    = 2500, // We just want 1 package
    .decode_fn      = &wg_pb12v1_callback,
    .disabled       = 0,
    .fields         = output_fields
};
