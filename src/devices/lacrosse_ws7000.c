/** @file
    LaCrosse WS7000/WS2500 weather sensors.

    Copyright (C) 2019 ReMiOS and Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
LaCrosse WS7000/WS2500 weather sensors.
Also sold by ELV and Conrad. Related to ELV WS 2000.

- WS2500-19 brightness sensor
- WS7000-20 meteo sensor (temperature/humidity/pressure)
- WS7000-16 Rain Sensor
- WS7000-15 wind sensor

PWM 400 us / 800 us with fixed bit width of 1200 us.
Messages are sent as nibbles (4 bits) with LSB sent first.
A frame is composed of a preamble followed by nibbles (4 bits) separated by a 1-bit.

Message Layout:

    P P S A D..D X C

- Preamble: 10x bit "0", bit "1"
- Sensor Type:  Value 0..9 determing the sensor type
  - 0 = WS7000-27/28 Thermo sensor (interval 177s - Addr * 0.5s)
  - 1 = WS7000-22/25 Thermo/Humidity sensor (interval 177s - Addr * 0.5s)
  - 2 = WS7000-16 Rain sensor (interval 173s - Addr * 0.5s)
  - 3 = WS7000-15 Wind sensor (interval 169s - Addr * 0.5s)
  - 4 = WS7000-20 Thermo/Humidity/Barometer sensor (interval 165s - Addr * 0.5s)
  - 5 = WS2500-19 Brightness sensor (interval 161s - Addr * 0.5s)
- Address:  Value 0..7 for the sensor address
  - In case of a negative temperature the MSB of the Address becomes "1"
- Data:     3-10 nibbles with BCD encoded sensor data values.
- XOR:      Nibble holding XOR of the S ^ A ^ Data nibbles
- Checksum: Sum of all nibbles + 5 (i.e. S + A + nibble(0) + .. + nibble(n) + XOR + 5) & 0xF

*/

#include "decoder.h"

static int lacrosse_ws7000_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0x01}; // 8 bits
    uint8_t const data_size[] = {3, 6, 3, 6, 10, 7}; // data nibbles by sensor type

    data_t *data;
    uint8_t b[14] = {0}; // LaCrosse WS7000-20 meteo sensor: 14 nibbles

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 8) + 8;
    if (start_pos >= bitbuffer->bits_per_row[0])
        return 0;

    unsigned max_bits = MIN(14 * 5, bitbuffer->bits_per_row[0] - start_pos);
    unsigned len      = extract_nibbles_4b1s(bitbuffer->bb[0], start_pos, max_bits, b);
    if (len < 7) // at least type, addr, 3 data, xor, add nibbles needed
        return 0;

    reflect_nibbles(b, len);

    int type = b[0];
    int addr = b[1] & 0x7;
    int id   = (type << 4) | addr;

    if (type > 5) {
        if (decoder->verbose > 1)
            fprintf(stderr, "LaCrosse-WS7000: unhandled sensor type (%d)\n", type);
        return 0;
    }

    unsigned data_len = data_size[type];
    if (len < data_len) {
        if (decoder->verbose > 1)
            fprintf(stderr, "LaCrosse-WS7000: short data (%u of %u)\n", len, data_len);
        return 0;
    }

    // check xor sum
    if (xor_bytes(b, len - 1)) {
        if (decoder->verbose > 1)
            fprintf(stderr, "LaCrosse-WS7000: checksum error (xor)\n");
        return 0;
    }

    // check add sum (all nibbles + 5)
    if (((add_bytes(b, len - 1) + 5) & 0xf) != b[len - 1]) {
        if (decoder->verbose > 1)
            fprintf(stderr, "LaCrosse-WS7000: checksum error (add)\n");
        return 0;
    }

    if (type == 0) {
        // 0 = WS7000-27/28 Thermo sensor
        int sign          = (b[1] & 0x8) ? -1 : 1;
        float temperature = ((b[4] * 10) + (b[3] * 1) + (b[2] * 0.1)) * sign;

        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "LaCrosse-WS7000-27/28",
                "id",               "",                 DATA_INT,    id,
                "channel",          "",                 DATA_INT,    addr,
                "temperature_C",    "Temperature",      DATA_DOUBLE, temperature,
                "mic",              "MIC",              DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    else if (type == 1) {
        // 1 = WS7000-22/25 Thermo/Humidity sensor
        int sign          = (b[1] & 0x8) ? -1 : 1;
        float temperature = ((b[4] * 10) + (b[3] * 1) + (b[2] * 0.1)) * sign;
        int humidity      = (b[7] * 10) + (b[6] * 1) + (b[5] * 0.1);

        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "LaCrosse-WS7000-22/25",
                "id",               "",                 DATA_INT,    id,
                "channel",          "",                 DATA_INT,    addr,
                "temperature_C",    "Temperature",      DATA_DOUBLE, temperature,
                "humidity",         "Humidity",         DATA_INT,    humidity,
                "mic",              "MIC",              DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    else if (type == 2) {
        // 2 = WS7000-16 Rain sensor
        int rain = (b[4] << 8) | (b[3] << 4) | (b[2]);

        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "LaCrosse-WS7000-16",
                "id",               "",                 DATA_INT,    id,
                "channel",          "",                 DATA_INT,    addr,
                "rain_mm",          "Rain counter",     DATA_DOUBLE, rain * 0.3,
                "mic",              "MIC",              DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    else if (type == 3) {
        // 3 = WS7000-15 Wind sensor
        float speed     = (b[4] * 10) + (b[3] * 1) + (b[2] * 0.1);
        float direction = ((b[7] >> 2) * 100) + (b[6] * 10) + (b[5] * 1);
        float deviation = (b[7] & 0x3) * 22.5;

        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "LaCrosse-WS7000-15",
                "id",               "",                 DATA_INT,    id,
                "channel",          "",                 DATA_INT,    addr,
                "wind_avg_km_h",    "Wind speed",       DATA_DOUBLE, speed,
                "wind_dir_deg",     "Wind direction",   DATA_DOUBLE, direction,
                "wind_dev_deg",     "Wind deviation",   DATA_DOUBLE, deviation,
                "mic",              "MIC",              DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    else if (type == 4) {
        // 4 = WS7000-20 Thermo/Humidity/Barometer sensor
        int sign          = (b[1] & 0x8) ? -1 : 1;
        float temperature = ((b[4] * 10) + (b[3] * 1) + (b[2] * 0.1)) * sign;
        int humidity      = (b[7] * 10) + (b[6] * 1) + (b[5] * 0.1);
        int pressure      = (b[10] * 100) + (b[9] * 10) + (b[8] * 1) + 200;

        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "LaCrosse-WS7000-20",
                "id",               "",                 DATA_INT,    id,
                "channel",          "",                 DATA_INT,    addr,
                "temperature_C",    "Temperature",      DATA_DOUBLE, temperature,
                "humidity",         "Humidity",         DATA_INT,    humidity,
                "pressure_hPa",     "Pressure",         DATA_INT,    pressure,
                "mic",              "MIC",              DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    else if (type == 5) {
        // 5 = WS2500-19 Brightness sensor
        unsigned brightness = (b[4] * 100) + (b[3] * 10) + (b[2] * 1);
        int b_exponent = b[5]; // 10^exp
        int exposition = (b[8] * 100) + (b[7] * 10) + (b[6] * 1);
        for (int i = b_exponent; i > 0; --i)
            brightness *= 10;

        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "LaCrosse-WS2500-19",
                "id",               "",                 DATA_INT,    id,
                "channel",          "",                 DATA_INT,    addr,
                "light_lux",        "Brightness",       DATA_INT,    brightness,
                "exposure_mins",    "Exposition",       DATA_INT,    exposition,
                "mic",              "MIC",              DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return 0; // should not be reached
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "rain_mm",
        "wind_avg_km_h",
        "wind_dir_deg",
        "wind_dev_deg",
        "temperature_C",
        "humidity",
        "pressure_hPa",
        "light_lux",
        "exposure_mins",
        "mic",
        NULL,
};

r_device lacrosse_ws7000 = {
        .name        = "LaCrosse/ELV/Conrad WS7000/WS2500 weather sensors",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 400,
        .long_width  = 800,
        .reset_limit = 1100,
        .decode_fn   = &lacrosse_ws7000_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
