/** @file
    Lidl Auriol 4-LD6654 (IAN 452207) temperature/humidity/rain sensor.

    Copyright (C) 2026 Giovanni Harting <539@idlegandalf.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Lidl Auriol 4-LD6654 (IAN 452207) temperature/humidity/rain sensor.

The outdoor sensor of the Auriol "RC weather station with rain gauge",
sold by Lidl, manufactured by digi-tech gmbh.

This is closely related to the Auriol 4-LD5661/4-LD5972/4-LD6313 family
(see auriol_4ld5661.c), sharing the modulation, the 52 bit frame length
and the first 28 bits of the layout. It differs in the second half of the
frame: where the 4-LD5661 family uses all 24 remaining bits as a rain
counter, this sensor uses 8 bits of humidity followed by a 16 bit rain
counter. The 4-LD5661 decoder therefore cannot decode it, as it requires
the byte at offset 3 to be exactly 0xf0.

The sensor transmits every ~57 seconds, repeating the frame 7 times.

Data layout:

    b1       81       05       f2       d0       09       e
    10110001 10000001 00000101 11110010 11010000 00001001 1110
    IIIIIIII B?CCTTTT TTTTTTTT FFFFHHHH HHHHRRRR RRRRRRRR RRRR

- I: id, 8 bit: factory (hard)coded random ID, changes on battery replacement
- B: battery, 1 bit: 1=OK, 0=LOW, assumed from the 4-LD5661 family, as the
  low state was not observed (all captures were taken on a fresh battery)
- ?: flag, 1 bit: always 0 in all observed frames
- C: channel, 2 bit: hardcoded to 0 on the observed unit
- T: temperature, 12 bit: 2's complement, scaled by 10
- F: 4 bit: constant 0xf, separator between temperature and humidity
- H: humidity, 8 bit: relative humidity in percent
- R: rain, 16 bit: tipping bucket counter, wraps at 65536

Format string:

    ID: hh BATT: b FLAG: b CH: bb TEMP: hhh SEP: h HUMI: hh RAIN: hhhh

There is no checksum or CRC in the frame, and no sync word, hence this
decoder is disabled by default, as is the related 4-LD5661 decoder.
For the same reason no "mic" key is reported in the output, the frame is
required to repeat, and out of range values are rejected.

Note that a 4-LD5661 frame passes the 0xf separator check, because that
family has 0xf0 at offset 3, which decodes here as a humidity of 0. Since
0 % relative humidity cannot occur outdoors, rejecting it keeps this decoder
from claiming frames of the sibling family.

The display unit multiplies the tip counter by 1.16 mm. This was measured by
tipping the bucket by hand and reading the display against the counter:

    counter 158 -> 0.0 mm, 163 -> 5.8 mm, 173 -> 17.4 mm, 176 -> 20.9 mm

which is exactly (counter - 158) * 1.16. The last point also rules out a
slightly smaller constant, as 1.155 would have displayed 20.8 mm.

Note that the rain counter in the sensor is free running and is not reset by
the display unit's rain reset, which only re-baselines the display. In the
sample above the display read 0.0 mm while the sensor counter was already at
158, so rain_mm reported here is a lifetime total and will not match the
display unit.
*/

#include "decoder.h"

static int auriol_4ld6654_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // The frame is repeated 7 times per transmission. With no checksum in the
    // protocol, agreement between repeats is the only error check available.
    int row = bitbuffer_find_repeated_row(bitbuffer, 3, 52);
    if (row < 0) {
        return DECODE_ABORT_LENGTH;
    }
    if (bitbuffer->bits_per_row[row] != 52) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[row];

    // No checksum in the frame, so lean on the constant fields instead:
    // the 0xf separator and the always-zero flag bit.
    if ((b[3] & 0xf0) != 0xf0 || (b[1] & 0x40) != 0) {
        return DECODE_ABORT_EARLY;
    }

    int id      = b[0];
    int batt_ok = b[1] >> 7;
    int channel = (b[1] & 0x30) >> 4;

    int temp_raw = (int16_t)(((b[1] & 0x0f) << 12) | (b[2] << 4)); // uses sign extend
    float temp_c = (temp_raw >> 4) * 0.1f;

    int humidity = ((b[3] & 0x0f) << 4) | (b[4] >> 4);

    int rain_raw = ((b[4] & 0x0f) << 12) | (b[5] << 4) | (b[6] >> 4);
    // The display unit counts 1.16 mm per tip. Scaled as double, as 1.16 is not
    // representable in float and the error shows up in the reported value.
    double rain_mm = rain_raw * 1.16;

    // A frame without a checksum is cheap to fake by noise, so reject values
    // the hardware cannot produce. A humidity of 0 also marks a frame of the
    // 4-LD5661 family, which carries 0xf0 at offset 3.
    if (humidity < 1 || humidity > 100 || temp_c < -40.0f || temp_c > 70.0f) {
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Auriol-4LD6654",
            "id",               "ID",           DATA_FORMAT, "%02x", DATA_INT, id,
            "channel",          "Channel",      DATA_INT,    channel,
            "battery_ok",       "Battery OK",   DATA_INT,    batt_ok,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "rain_mm",          "Rain",         DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_mm,
            "rain",             "Rain tips",    DATA_INT,    rain_raw,
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
        "rain_mm",
        "rain",
        NULL,
};

r_device const auriol_4ld6654 = {
        .name        = "Auriol 4-LD6654 temperature/humidity/rain sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .sync_width  = 2500,
        .gap_limit   = 2500,
        .reset_limit = 4000,
        .decode_fn   = &auriol_4ld6654_decode,
        .disabled    = 1, // no sync-word, no fix id, no checksum
        .fields      = output_fields,
};
