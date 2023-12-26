/** @file
    Steelmate TPMS FSK protocol.

    Copyright (C) 2016 Benjamin Larsson
    Copyright (C) 2016 John Jore

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Steelmate TPMS FSK protocol.

Packet payload: 9 bytes.

Bytes 2 to 9 are inverted Manchester with swapped MSB/LSB:

                                  0  1  2  3  4  5  6  7  8
                       [00] {72} 00 00 7f 3c f0 d7 ad 8e fa
    After translating            00 00 01 c3 f0 14 4a 8e a0
                                 SS SS AA II II PP TT BB CC

- S = sync, (0x00)
- A = preamble, (0x01)
- I = id, 0xc3f0
- P = Pressure as double the PSI, 0x14 = 10 PSI
- T = Temperature in Fahrenheit, 0x4a = 74 'F
- B = Battery as half the millivolt, 0x8e = 2.84 V
- C = Checksum, adding bytes 2 to 7 modulo 256 = byte 8,(0x01+0xc3+0xf0+0x14+0x4a+0x8e) modulus 256 = 0xa0

*/

#include "decoder.h"

static int steelmate_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    //Loop through each row of data
    for (int row = 0; row < bitbuffer->num_rows; row++)
    {
        //Payload is inverted Manchester encoded, and reversed MSB/LSB order
        uint8_t *b = bitbuffer->bb[row];

        //Length must be 72 bits to be considered a valid packet
        if (bitbuffer->bits_per_row[row] != 72)
            continue; // DECODE_ABORT_LENGTH

        //Valid preamble? (Note, the data is still wrong order at this point. Correct pre-amble: 0x00 0x00 0x01)
        if (b[0] != 0x00 || b[1] != 0x00 || b[2] != 0x7f)
            continue; // DECODE_ABORT_EARLY

        //Preamble
        uint8_t preamble = ~reverse8(b[2]);

        //Sensor ID
        uint8_t id1 = ~reverse8(b[3]);
        uint8_t id2 = ~reverse8(b[4]);

        //Pressure is stored as twice the PSI
        uint8_t p1 = ~reverse8(b[5]);

        //Temperature is stored in Fahrenheit. Note that the datasheet claims operational to -40'C, but can only express values from -17.8'C
        uint8_t tempFahrenheit = ~reverse8(b[6]);

        //Battery voltage is stored as half the mV
        uint8_t tmpbattery_mV = ~reverse8(b[7]);

        //Checksum is a sum of all the other values
        uint8_t payload_checksum = ~reverse8(b[8]);
        uint8_t calculated_checksum = preamble + id1 + id2 + p1 + tempFahrenheit + tmpbattery_mV;
        if (payload_checksum != calculated_checksum)
            continue; // DECODE_FAIL_MIC

        int sensor_id      = (id1 << 8) | id2;
        float pressure_psi = p1 * 0.5f;
        int battery_mV     = tmpbattery_mV * 2;

        char sensor_idhex[7];
        snprintf(sensor_idhex, sizeof(sensor_idhex), "0x%04x", sensor_id);

        /* clang-format off */
        data_t *data = data_make(
                "type",             "",             DATA_STRING, "TPMS",
                "model",            "",             DATA_STRING, "Steelmate",
                "id",               "",             DATA_STRING, sensor_idhex,
                "pressure_PSI",     "",             DATA_DOUBLE, pressure_psi,
                "temperature_F",    "",             DATA_DOUBLE, (float)tempFahrenheit,
                "battery_mV",       "",             DATA_INT,    battery_mV,
                "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    //Was not a Steelmate TPMS after all
    // TODO: improve return codes by aborting early
    return DECODE_FAIL_SANITY;
}

static char const *const output_fields[] = {
        "type",
        "model",
        "id",
        "pressure_PSI",
        "temperature_F",
        "battery_mV",
        "mic",
        NULL,
};

r_device const steelmate = {
        .name        = "Steelmate TPMS",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 12 * 4,
        .long_width  = 0,
        .reset_limit = 27 * 4,
        .decode_fn   = &steelmate_callback,
        .fields      = output_fields,
};
