/** @file
    Shenzhen Calibeur Industries Co. Ltd Wireless Thermometer RF-104 Temperature/Humidity sensor.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Shenzhen Calibeur Industries Co. Ltd Wireless Thermometer RF-104 Temperature/Humidity sensor.

RF-104 Temperature/Humidity sensor
aka Biltema Art. 84-056 (Sold in Denmark)
aka ...

NB. Only 3 unique sensors can be detected!

Update (LED flash) each 2:53

Pulse Width Modulation with fixed rate and startbit

    Startbit     = 390 samples = 1560 µs
    Short pulse  = 190 samples =  760 µs = Logic 0
    Long pulse   = 560 samples = 2240 µs = Logic 1
    Pulse rate   = 740 samples = 2960 µs
    Burst length = 81000 samples = 324 ms

Sequence of 5 times 21 bit separated by start bit (total of 111 pulses)

    S 21 S 21 S 21 S 21 S 21 S

- Channel number is encoded into fractional temperature
- Temperature is oddly arranged and offset for negative temperatures = [6543210] - 41 C
- Always an odd number of 1s (odd parity)

Encoding legend:

    f = fractional temperature + [ch no] * 10
    0-6 = integer temperature + 41C
    p = parity
    H = Most significant bits of humidity [5:6]
    h = Least significant bits of humidity [0:4]

    LSB                 MSB
    ffffff45 01236pHH hhhhh Encoding

*/

#include "decoder.h"

static int calibeur_rf104_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t id;
    float temperature;
    float humidity;
    uint8_t *b = bitbuffer->bb[1];
    uint8_t *b2 = bitbuffer->bb[2];

    // row [0] is empty due to sync bit
    // No need to decode/extract values for simple test
    // check for 0x00 and 0xff
    if ((!b[0] && !b[1] && !b[2])
            || (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff)) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00 or 0xFF");
        return DECODE_FAIL_SANITY;
    }

    bitbuffer_invert(bitbuffer);
    // Validate package (row [0] is empty due to sync bit)
    if (bitbuffer->bits_per_row[1] != 21) // Don't waste time on a long/short package
        return DECODE_ABORT_LENGTH;
    if (crc8(b, 3, 0x80, 0) == 0) // It should be odd parity
        return DECODE_FAIL_MIC;
    if ((b[0] != b2[0]) || (b[1] != b2[1]) || (b[2] != b2[2])) // We want at least two messages in a row
        return DECODE_FAIL_SANITY;

    uint8_t bits;

    bits  = ((b[0] & 0x80) >> 7);   // [0]
    bits |= ((b[0] & 0x40) >> 5);   // [1]
    bits |= ((b[0] & 0x20) >> 3);   // [2]
    bits |= ((b[0] & 0x10) >> 1);   // [3]
    bits |= ((b[0] & 0x08) << 1);   // [4]
    bits |= ((b[0] & 0x04) << 3);   // [5]
    id = bits / 10;
    temperature = (float)(bits % 10) * 0.1f;

    bits  = ((b[0] & 0x02) << 3);   // [4]
    bits |= ((b[0] & 0x01) << 5);   // [5]
    bits |= ((b[1] & 0x80) >> 7);   // [0]
    bits |= ((b[1] & 0x40) >> 5);   // [1]
    bits |= ((b[1] & 0x20) >> 3);   // [2]
    bits |= ((b[1] & 0x10) >> 1);   // [3]
    bits |= ((b[1] & 0x08) << 3);   // [6]
    temperature += (float)bits - 41.0f;

    bits  = ((b[1] & 0x02) << 4);   // [5]
    bits |= ((b[1] & 0x01) << 6);   // [6]
    bits |= ((b[2] & 0x80) >> 7);   // [0]
    bits |= ((b[2] & 0x40) >> 5);   // [1]
    bits |= ((b[2] & 0x20) >> 3);   // [2]
    bits |= ((b[2] & 0x10) >> 1);   // [3]
    bits |= ((b[2] & 0x08) << 1);   // [4]
    humidity = bits;

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",            DATA_STRING, "Calibeur-RF104",
            "id",            "ID",          DATA_INT,    id,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
            "humidity",      "Humidity",    DATA_FORMAT, "%2.0f %%", DATA_DOUBLE, humidity,
            "mic",           "Integrity",   DATA_STRING, "CRC",
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device const calibeur_RF104 = {
        .name        = "Calibeur RF-104 Sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 760,  // Short pulse 760µs
        .long_width  = 2240, // Long pulse 2240µs
        .reset_limit = 3200, // Longest gap (2960-760µs)
        .sync_width  = 1560, // Startbit 1560µs
        .decode_fn   = &calibeur_rf104_decode,
        .fields      = output_fields,
};
