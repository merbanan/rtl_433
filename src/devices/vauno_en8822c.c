/** @file
    Vauno EN8822C sensor on 433.92Mhz.

    Copyright (C) 2022 Jamie Barron <gumbald@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Largely the same as Esperanza EWS, s3318p.
@sa esperanza_ews.c s3318p.c

List of known supported devices:
- EN8822C-1

Frame structure:

    Byte:      0        1        2        3        4
    Nibble:    1   2    3   4    5   6    7   8    9   10   11
    Type:      IIIIIIII ??CCTTTT TTTTTTTT HHHHHHH? FFFFXXXX XX

- I: Random device ID
- C: Channel (1-3)
- T: Temperature (Little-endian)
- H: Humidity (Little-endian)
- F: Flags (unknown)
- X: 

Sample Data:

    ```
    [00] {42} af 0f a2 7c 01 c0 : 10101111 00001111 10100010 01111100 00000001 11
    Sensor ID	= 175 = 0xaf
    Channel		= 0
    temp		= -93 = 0x111110100010
    TemperatureC	= -9.3
    hum    = 62% = 0x0111110
    ```

*/

#include "decoder.h"

static int vauno_en8822c_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t b[6];
    data_t *data;

    // require two leading sync pulses
    if (bitbuffer->bits_per_row[0] != 0 || bitbuffer->bits_per_row[1] != 0)
        return DECODE_FAIL_SANITY;

    if (bitbuffer->num_rows != 14)
        return DECODE_ABORT_LENGTH;

    for (int row = 2; row < bitbuffer->num_rows - 3; row += 2) {
        if (memcmp(bitbuffer->bb[row], bitbuffer->bb[row + 2], sizeof(bitbuffer->bb[row])) != 0
                || bitbuffer->bits_per_row[row] != 42)
            return DECODE_FAIL_SANITY;
    }
    int r = 2;

    // remove the two leading 0-bits and align the data
    bitbuffer_extract_bytes(bitbuffer, r, 2, b, 42);

    // checksum is addition
    int chk = ((b[4] & 0x0f) << 2) | (b[5] & 0x03);
    if (add_nibbles(b, 4) != chk)
        return DECODE_FAIL_MIC;

    int device_id = b[0];
    int channel   = ((b[1] & 0x30) >> 4) + 1;
    // Battery status is the 7th bit 0x40. 0 = normal, 1 = low
    //unsigned char const battery_low = (b[4] & 0x40) == 0x40;
    int temp_raw  = ((b[2] & 0x0f) << 8) | (b[2] & 0xf0) | (b[1] & 0x0f);
    float temp_c  = (temp_raw) * 0.1f;
    int humidity  = ((b[3] & 0xf0) >> 3);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Vauno-EN8822C",
            "id",               "ID",           DATA_INT, device_id,
            "channel",          "Channel",      DATA_INT, channel,
    //        "battery_ok",       "Battery",      DATA_INT, !battery_low,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
//        "battery_ok",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device vauno_en8822c = {
        .name        = "Vauno EN8822C",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .gap_limit   = 4200,
        .reset_limit = 9100,
        .decode_fn   = &vauno_en8822c_callback,
        .fields      = output_fields,
};
