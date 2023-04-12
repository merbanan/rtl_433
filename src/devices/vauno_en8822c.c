/** @file
    Vauno EN8822C sensor on 433.92MHz.

    Copyright (C) 2022 Jamie Barron <gumbald@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Vauno EN8822C sensor on 433.92MHz.

Largely the same as Esperanza EWS, s3318p.
@sa esperanza_ews.c s3318p.c

List of known supported devices:
- Vauno EN8822C-1
- FUZHOU ESUN ELECTRONIC outdoor T21 sensor

Frame structure (42 bits):

    Byte:      0        1        2        3        4
    Nibble:    1   2    3   4    5   6    7   8    9   10   11
    Type:      IIIIIIII B?CCTTTT TTTTTTTT HHHHHHHF FFFBXXXX XX

- I: Random device ID
- C: Channel (1-3)
- T: Temperature (Little-endian)
- H: Humidity (Little-endian)
- F: Flags (unknown)
- B: Battery (1=low voltage ~<2.5V)
- X: Checksum (6 bit nibble sum)

Sample Data:

    [00] {42} af 0f a2 7c 01 c0 : 10101111 00001111 10100010 01111100 00000001 11

- Sensor ID	= 175 = 0xaf
- Channel		= 0
- temp		= -93 = 0x111110100010
- TemperatureC	= -9.3
- hum    = 62% = 0x0111110

*/

#include "decoder.h"

static int vauno_en8822c_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_prefix(bitbuffer, 4, 42);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *b = bitbuffer->bb[row];

    // checksum is addition
    int chk = ((b[4] & 0x0f) << 2) | (b[5] >> 6);
    int sum = add_nibbles(b, 4) + (b[4] >> 4);
    if (sum == 0) {
        return DECODE_ABORT_EARLY; // reject all-zeros
    }
    if ((sum & 0x3f) != chk) {
        return DECODE_FAIL_MIC;
    }

    int device_id = b[0];
    int channel   = ((b[1] & 0x30) >> 4) + 1;
    int battery_low = (b[4] & 0x10) >> 4;
    int temp_raw = (int16_t)(((b[1] & 0x0f) << 12) | ((b[2] & 0xff) << 4));
    float temp_c  = (temp_raw >> 4) * 0.1f;
    int humidity  = (b[3] >> 1);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Vauno-EN8822C",
            "id",               "ID",           DATA_INT, device_id,
            "channel",          "Channel",      DATA_INT, channel,
            "battery_ok",       "Battery",      DATA_INT, !battery_low,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device const vauno_en8822c = {
        .name        = "Vauno EN8822C",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .tolerance   = 500,
        .gap_limit   = 5000,
        .reset_limit = 9500,
        .decode_fn   = &vauno_en8822c_decode,
        .fields      = output_fields,
};
