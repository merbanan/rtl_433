/** @file
    Schou 72543 Day Rain Gauge.

    contributed by Jesper M. Nielsen
    discovered by Jesper M. Nielsen
    based upon ambient_weather.c

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decode Schou 72543 Rain Gauge, DAY series.

Devices supported:

- Schou 72543 Rain Gauge, DAY Series.
- Motonet MTX rain gauge (Product code: 86-01352) sold in Finland.
- MarQuant Wireless Rain Gauge (Product code: 014369) sold by JULA AB, Sweden.

This decoder handles the 433mhz rain-thermometer.

Codes example: {66}50fc467b7f9a832a8, {65}a1f88cf6ff3506550, {70}a1f88cf6ff3506557c

    {66}: [ 0 ] [ 1010 0001 1111 1000 ] [ 1000 ] [ 1100 ] [ 1111 0110 ] [ 1111 1111 ] [ 0011 0101 ] [ 0000 0110 ] [ 0101 0101 ] [ 0       ]
    {65}:       [ 1010 0001 1111 1000 ] [ 1000 ] [ 1100 ] [ 1111 0110 ] [ 1111 1111 ] [ 0011 0101 ] [ 0000 0110 ] [ 0101 0101 ] [ 0       ]
    {70}:       [ 1010 0001 1111 1000 ] [ 1000 ] [ 1100 ] [ 1111 0110 ] [ 1111 1111 ] [ 0011 0101 ] [ 0000 0110 ] [ 0101 0101 ] [ 0111 11 ]
    KEY:  [ 0 ] [ IIII IIII IIII IIII ] [ SSSS ] [ NNNN ] [ rrrr rrrr ] [ RRRR RRRR ] [ tttt tttt ] [ TTTT TTTT ] [ CCCC CCCC ] [ 0??? ?? ]

- 0:  Always zero
- ?:  Either 1 or 0
- I:  16 bit random ID. Resets to new value after every battery change
- S:  Status bits
      [ X--- ]: Battery status:  0: OK,  1: Low battery
      [ -X-- ]: Repeated signal: 0: New, 1: Repeat of last message (4 repeats will happen after battery replacement)
      [ --XX ]: Assumed always to be 0
- N:  4 bit running count. Increased by 2 every value incremented by 2 every message, i.e. 0, 2, 4, 6, 8, a, c, e, 0, 2...
- Rr: 16 bit Rainfall in 1/10 millimeters per count. Initial value fff6 = 6552.6 mm rain
      r: lower 8 bit, initializes to f6
      R: Upper 8 bit, initializes to ff
- Tt: 16 bit temperature.
      t: lower 8 bit
      T: Upper 8 bit
- C:  Checksum. Running 8 bit sum of the data left of the checksum.
      E.g. {65}a1f88cf6ff3506'55'0 Checksum is 55 obtained as ( a1 + f8 + 8c + f6 + ff + 35 + 06 ) = 455 i.e. 55

*/

static int schou_72543_rain_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Full data is 3 rows, two are required for data validation
    if (bitbuffer->num_rows < 2) {
        return DECODE_ABORT_LENGTH;
    }

    // Check if the first 64 bits of at least two rows are alike
    int row = bitbuffer_find_repeated_prefix(bitbuffer, 2, 64);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    // Load bitbuffer data and validate checksum
    uint8_t *b = bitbuffer->bb[row];
    int micsum = b[7];                    // Checksum as read
    int calsum = add_bytes(b, 7) & 0x0FF; // Checksum as calculated, accounting for the lowest 8 bit

    if (micsum != calsum) {
        decoder_logf_bitrow(decoder, 1, __func__, b, 65, "Checksum error, expected: %02x calculated: %02x", micsum, calsum);
        return DECODE_FAIL_MIC;
    }

    // Decode message
    int device_id       = (b[0] << 8) | b[1];                  // Assuming little endian, but it not important as the value is random
    int battery_low     = (b[2] & 0x80) > 0;                   // if one, battery is low
    int message_repeat  = (b[2] & 0x40) > 0;                   // if one, message is a repeat (startup after batteries are replaced)
    int message_counter = (b[2] & 0x0e) >> 1;                  // 3 bit counter (rather than 4 bit incrementing by 2 each time
    float rain_mm       = ((b[4] << 8) | b[3]) * 0.1f;         //   0.0 to  6553.5  mm
    float temperature_F = (((b[6] << 8) | b[5]) - 900) * 0.1f; // -40.0 to +158     degF

    /* clang-format off */
    data_t   *data = data_make(
            "model",            "",             DATA_STRING, "Schou-72543",
            "id",               "ID",           DATA_INT,    device_id,
            "temperature_F",    "Temperature",  DATA_FORMAT, "%.1f F",  DATA_DOUBLE, temperature_F,
            "rain_mm",          "Rain",         DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_mm,
            "battery_ok",       "Battery_ok",   DATA_INT,    !battery_low,
            "msg_counter",      "Counter",      DATA_INT,    message_counter,
            "msg_repeat",       "Msg_repeat",   DATA_INT,    message_repeat,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_F",
        "rain_mm",
        "battery_ok",
        "msg_counter",
        "msg_repeat",
        "mic",
        NULL,
};

r_device const schou_72543_rain = {
        .name        = "Schou 72543 Day Rain Gauge, Motonet MTX Rain, MarQuant Rain Gauge",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 972,
        .long_width  = 2680,
        .sync_width  = 7328,
        .reset_limit = 2712,
        .decode_fn   = &schou_72543_rain_decode,
        .fields      = output_fields,
};
