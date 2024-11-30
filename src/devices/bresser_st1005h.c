/** @file
    Bresser ST1005H sensor protocol.

    Copyright (C) 2024 David Kalnischkies <david@kalnischkies.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Bresser ST1005H sensor protocol.

The protocol is for a(nother) variant of wireless Temperature/Humidity sensor
- Bresser Thermo-/Hygro-Sensor 3CH [7009984]
  https://www.bresser.com/p/bresser-thermo-hygro-sensor-7009984
  A "Bresser" sticker is covering the "EXPLORE SCIENTIFIC" logo on the front
  Multi-Language Manual is branded "EXPLORE® SCIENTIFIC" Art.No.: ST1005H

Another sensor sold under the same generic name is handled by bresser_3ch.c.


The data of this sensor is grouped in 38 bits that are repeated a few times,
and is send roughly every 90 secs (plus each time TX button is pressed).

The data has the following categorization of the bits:
01234567 89012345 67890123 45678901 234567
0IIIIIII ILBCCTTT TTTTTTTT THHHHHHH ======

where:
  0 prefixed always null bit
  I identity (changed by battery replacement)
  L low battery (assumed, always 0 in tests)
  B triggered by TX button in battery compartment
  C channel 1-3 choosen by switch in battery compartment
  T temperature in °C (with one decimal) multiplied by 10
  H humidity, values higher than 95 are shown as HH in display
  = checksum of nibbles added


Examples with their temp/humanity reading in display:
0 12345678 9 0 12 345678901234 5678901 234567
0 IIIIIIII L B CC TTTTTTTTTTTT HHHHHHH ======   hum  temp
0 01111101 0 0 00 000010110001 1000110 110100   70%  17.7°
0 01111101 0 0 00 000010110010 1000110 110101   70%  17.8°
0 01111101 0 1 00 000010110011 1000110 111010   70%  17.9°
0 01111101 0 1 00 000010110011 1001001 110001   73%  17.9°
0 01111101 0 1 00 000010110100 1000111 111101   71%  18.0°
0 01111101 0 1 00 000010110101 1000101 111010   69%  18.1°
0 11011101 0 0 00 000010110101 1000101 111100   69%  18.1°
0 01001010 0 0 00 000010110101 1000110 110010   70%  18.1°
0 10100000 0 0 00 000010110110 1000100 101011   68%  18.2°
0 01101010 0 0 00 000010110110 1000101 110011   69%  18.2°
0 01101010 0 1 00 000010110110 1000100 110101   68%  18.2°
0 01101010 0 0 00 000010110011 1000101 110000   69%  17.9°
0 01101010 0 1 00 000010110011 1000101 110100   69%  17.9°
0 01101010 0 0 00 000011000101 1101110 111010   HH%  19.7°
0 01101010 0 0 00 000011010000 1101110 110110   HH%  20.8°
0 01101010 0 0 00 000011010011 1011111 111001   95%  21.1°
0 11000010 0 1 00 111101011100 1010011 000010   83% -16.4°
0 11000010 0 1 00 111101110100 1001110 000001   78% -14.0°
0 11000010 0 1 00 111110100010 1101110 000110   HH%  -9.4°
0 11000010 0 1 00 000000001101 1011101 110100   93%   1.3°
0 11110000 0 0 00 000010011100 1010010 110010   82%  15.6°
0 11110000 0 1 00 000010011100 1010010 110110   82%  15.6°
0 11110000 0 1 01 000010011100 1010010 110111   82%  15.6°
0 11110000 0 1 10 000010011100 1010010 111000   82%  15.6°

The device has a second button in the battery compartment to flip
between °C and °F in the display (default is °C), but its state
does not change the transmission in any way.

The device "Oregon Scientific SL109H Remote Thermal Hygro Sensor" works
with the same row length, but a completely different interpretation.
As such, if the bits align both decoders can misdetect data from the
other sensor as valid from their sensor with "plausable" but usually
completely wrong values.

Examples which are misdetected by Oregon:
0 01101010 0 1 00 000010101011 1000110 111101   70%  17.1°
0 11000010 0 0 00 000001010010 1101110 101110   HH%   8.2°
*/
#include "decoder.h"

/* helper for the checksum adding the nibbles of given value */
static int bresser_add_nibbles(int const input, int const count)
{
    int output = 0;
    for (int i = 0; i < (count * 4); i += 4)
        output += ((input & (0x000F << i)) >> i);
    return output;
}
static int bresser_st1005h_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;

    int id, button, battery_low, channel, temp_raw, humidity, checksum;
    double temp_c;

    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 38);
    if (r < 0 || bitbuffer->bits_per_row[r] > 38) {
        return DECODE_ABORT_LENGTH;
    }
    b = bitbuffer->bb[r];

    if (bitrow_get_bit(b, 0) != 0) {
        decoder_log(decoder, 1, __func__, "prefix null bit is not null");
        return DECODE_FAIL_SANITY;
    }

    id          = bitrow_get_byte(b, 1);
    battery_low = bitrow_get_bit(b, 9);
    button      = bitrow_get_bit(b, 10);
    channel     = (bitrow_get_bit(b, 11) << 1) + bitrow_get_bit(b, 12) + 1;

    temp_raw = bitrow_get_bit(b, 13) ? ~0xFFF : 0;
    temp_raw |= (bitrow_get_byte(b, 13) << 4) + (bitrow_get_byte(b, 21) >> 4);
    temp_c = temp_raw * 0.1;

    humidity = bitrow_get_byte(b, 25) >> 1;

    checksum = (bresser_add_nibbles(id, 2) + (battery_low << 3) + (button << 2) + (channel - 1) + bresser_add_nibbles(temp_raw, 3) + bresser_add_nibbles(2 * humidity, 2)) & 0x03F;
    if (checksum != (b[4] >> 2)) {
        decoder_log(decoder, 1, __func__, "checksum error");
        return DECODE_FAIL_MIC;
    }

    if ((channel >= 4) || (humidity > 110) || (temp_c < -30.0) || (temp_c > 160.0)) {
        decoder_log(decoder, 1, __func__, "data error");
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, "Bresser-ST1005H",
            "id",            "Id",          DATA_INT,    id,
            "channel",       "Channel",     DATA_INT,    channel,
            "battery_ok",    "Battery",     DATA_INT,    !battery_low,
            "button",        "Button",      DATA_INT,    button,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",           "Integrity",   DATA_STRING, "CHECKSUM",
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
        "button",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device const bresser_st1005h = {
        .name        = "Bresser Thermo-/Hygro-Sensor Explore Scientific ST1005H",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2500,
        .long_width  = 4500,
        .gap_limit   = 4500,
        .reset_limit = 10000,
        .decode_fn   = &bresser_st1005h_decode,
        .fields      = output_fields,
};
