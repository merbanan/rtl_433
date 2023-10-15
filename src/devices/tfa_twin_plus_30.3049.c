/** @file
    TFA-Twin-Plus-30.3049
    also Conrad KW9010 (perhaps just rebranded), Ea2 BL999.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>
    original implementation 2015 Paul Ortyl

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Decode TFA-Twin-Plus-30.3049, Conrad KW9010 (perhaps just rebranded), Ea2 BL999.

Protocol as reverse engineered by https://github.com/iotzo

36 Bits (9 nibbles)

| Type: | IIIICCII | B???TTTT | TTTTTSSS | HHHHHHH1 | XXXX |
| ----- | -------- | -------- | -------- | -------- | ---- |
| BIT/8 | 76543210 | 76543210 | 76543210 | 76543210 | 7654 |
| BIT/A | 01234567 | 89012345 | 57890123 | 45678901 | 2345 |
|       | 0        | 1        | 2        | 3        |      |

- I: sensor ID (changes on battery change)
- C: Channel number
- B: low battery
- T: temperature
- S: sign
- X: checksum
- ?: unknown meaning
- all values are LSB-first, so need to be reversed before presentation

    [04] {36} e4 4b 70 73 00 : 111001000100 101101110 000 0111001 10000 ---> temp/hum:23.7/50
    temp num-->13-21bit(9bits) in reverse order in this case "011101101"=237
    positive temps (with 000 in bits 22-24) : temp=num/10 (in this case 23.7 C)
    negative temps (with 111 in bits 22-24) : temp=(512-num)/10
    negative temps example:
    [03] {36} e4 4c 1f 73 f0 : 111001000100 110000011 111 0111001 11111 temp: -12.4

    Humidity:
    hum num-->25-32bit(7bits) in reverse order : in this case "1001110"=78
    humidity=num-28 --> 78-28=50

I have channel number bits(5,6 in reverse order) and low battery bit(9).
It seems that the 1,2,3,4,7,8 bits changes randomly on every reset/battery change.
*/

#include "decoder.h"

static int tfa_twin_plus_303049_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int row;
    uint8_t *b;

    row = bitbuffer_find_repeated_row(bitbuffer, 2, 36);
    if (row < 0)
        return DECODE_ABORT_EARLY;

    if (bitbuffer->bits_per_row[row] != 36)
        return DECODE_ABORT_LENGTH;

    b = bitbuffer->bb[row];

    if (!(b[0] || b[1] || b[2] || b[3] || b[4])) /* exclude all zeros */
        return DECODE_ABORT_EARLY;

    // reverse bit order
    uint8_t rb[5] = { reverse8(b[0]), reverse8(b[1]), reverse8(b[2]),
            reverse8(b[3]), reverse8(b[4]) };

    int sum_nibbles =
        (rb[0] >> 4) + (rb[0] & 0xF)
      + (rb[1] >> 4) + (rb[1] & 0xF)
      + (rb[2] >> 4) + (rb[2] & 0xF)
      + (rb[3] >> 4) + (rb[3] & 0xF);

    int checksum = rb[4] & 0x0F;  // just make sure the 10th nibble does not contain junk
    if (checksum != (sum_nibbles & 0xF))
        return DECODE_FAIL_MIC; // wrong checksum

  /* IIIICCII B???TTTT TTTTTSSS HHHHHHH1 XXXX */
    int negative_sign = (b[2] & 7);
    int temp          = ((rb[2]&0x1F) << 4) | (rb[1]>> 4);
    int humidity      = (rb[3] & 0x7F) - 28;
    int sensor_id     = (rb[0] & 0x0F) | ((rb[0] & 0xC0)>>2);
    int battery_low   = b[1] >> 7;
    int channel       = (b[0]>>2) & 3;

    float tempC = (negative_sign ? -((1 << 9) - temp) : temp) * 0.1F;

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, "TFA-TwinPlus",
            "id",            "Id",          DATA_INT,    sensor_id,
            "channel",       "Channel",     DATA_INT,    channel,
            "battery_ok",    "Battery",     DATA_INT,    !battery_low,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, tempC,
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
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device const tfa_twin_plus_303049 = {
        .name        = "TFA-Twin-Plus-30.3049, Conrad KW9010, Ea2 BL999",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .gap_limit   = 6000,
        .reset_limit = 10000,
        .decode_fn   = &tfa_twin_plus_303049_callback,
        .fields      = output_fields,
};
