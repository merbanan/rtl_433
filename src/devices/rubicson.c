/** @file
    Rubicson or InFactory PT-310 temperature sensor.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/** @fn int rubicson_callback(r_device *decoder, bitbuffer_t *bitbuffer)
Rubicson temperature sensor.

Also older TFA 30.3197 sensors.

Also InFactory PT-310 pool temperature sensor (AKA ZX-7074/7073). This device
has longer packet lengths of 37 or 38 bits but is otherwise compatible. See more at
https://github.com/merbanan/rtl_433/issues/2119

The sensor sends 12 packets of  36 bits pwm modulated data.

data is grouped into 9 nibbles

    [id0] [id1] [bat|unk1|chan1|chan2] [temp0] [temp1] [temp2] [0xf] [crc1] [crc2]

- The id changes when the battery is changed in the sensor.
- bat bit is 1 if battery is ok, 0 if battery is low
- unk1 is always 0 probably unused
- chan1 and chan2 forms a 2bit value for the used channel
- temp is 12 bit signed scaled by 10
- F is always 0xf
- crc1 and crc2 forms a 8-bit crc, polynomial 0x31, initial value 0x6c, final value 0x0

The sensor can be bought at Kjell&Co. The Infactory pool sensor can be bought at Pearl.
*/

#include "decoder.h"

// NOTE: this is used in nexus.c and solight_te44.c
int rubicson_crc_check(uint8_t *b);

int rubicson_crc_check(uint8_t *b)
{
    uint8_t tmp[5];
    tmp[0] = b[0];                // Byte 0 is nibble 0 and 1
    tmp[1] = b[1];                // Byte 1 is nibble 2 and 3
    tmp[2] = b[2];                // Byte 2 is nibble 4 and 5
    tmp[3] = b[3] & 0xf0;         // Byte 3 is nibble 6 and 0-padding
    tmp[4] = (b[3] & 0x0f) << 4 | // CRC is nibble 7 and 8
             (b[4] & 0xf0) >> 4;

    return crc8(tmp, 5, 0x31, 0x6c) == 0;
}

static int rubicson_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int id, battery, channel, temp_raw;
    float temp_c;

    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 36);
    if (r < 0)
        return DECODE_ABORT_EARLY;

    b = bitbuffer->bb[r];

    // Infactory devices report 38 (or for last repetition) 37 bits
    if (bitbuffer->bits_per_row[r] < 36 || bitbuffer->bits_per_row[r] > 38)
        return DECODE_ABORT_LENGTH;

    if ((b[3] & 0xf0) != 0xf0)
        return DECODE_ABORT_EARLY; // const not 1111

    if (!rubicson_crc_check(b))
        return DECODE_FAIL_MIC;

    id       = b[0];
    battery  = (b[1] & 0x80);
    channel  = ((b[1] & 0x30) >> 4) + 1;
    temp_raw = (int16_t)((b[1] << 12) | (b[2] << 4)); // sign-extend
    temp_c   = (temp_raw >> 4) * 0.1f;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Rubicson-Temperature",
            "id",               "House Code",   DATA_INT,    id,
            "channel",          "Channel",      DATA_INT,    channel,
            "battery_ok",       "Battery",      DATA_INT,    !!battery,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "mic",              "Integrity",    DATA_STRING, "CRC",
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
        "mic",
        NULL,
};

// timings based on samp_rate=1024000
r_device const rubicson = {
        .name        = "Rubicson, TFA 30.3197 or InFactory PT-310 Temperature Sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000, // Gaps:  Short 976us, Long 1940us, Sync 4000us
        .long_width  = 2000, // Pulse: 500us (Initial pulse in each package is 388us)
        .gap_limit   = 3000,
        .reset_limit = 4800, // Two initial pulses and a gap of 9120us is filtered out
        .decode_fn   = &rubicson_callback,
        .fields      = output_fields,
};
