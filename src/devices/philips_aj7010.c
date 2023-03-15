/** @file
    Philips outdoor temperature sensor.

    Copyright (C) 2018 Nicolas Jourden <nicolas.jourden@laposte.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Philips outdoor temperature sensor -- used with various Philips clock
radios (tested on AJ7010).
This is inspired from the other Philips driver made by Chris Coffey.

A complete message is 40 bits:
- 3 times sync of 1000us pulse + 1000us gap.
- 40 bits, 2000 us short or 6000 us long
- packet gap is 38 ms
- Packets are repeated 3 times.

40-bit data packet format:

    00000000 01000101 00100010 00101001 01001110 : g_philips_21.1_ch2_B.cu8
    00000000 01011010 01111100 00101001 00001111 : g_philips_21.4_ch1_C.cu8
    00000000 01011010 00000101 00100110 01111001 : gph_bootCh1_17.cu8
    00000000 01000101 00011110 00100110 01111101 : gph_bootCh2_17.cu8
    00000000 00110110 11100011 00100101 11110000 : gph_bootCh3_17.cu8

Data format is:

    00000000  0ccccccc tttttttt TTTTTTTT XXXXXXXX

- c: 7 bit channel: 0x5A=channel 1, 0x45=channel 2, 0x36=channel 3
- t: 16 bit temperature in ADC value that is then converted to deg. C.
- X: XOR sum, every 2nd packet without last data byte (T).
*/

#include "decoder.h"

static int philips_aj7010_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    // Correct number of rows?
    if (bitbuffer->num_rows != 1) {
        decoder_logf(decoder, 1, __func__, "wrong number of rows (%d)", bitbuffer->num_rows);
        return DECODE_ABORT_LENGTH;
    }

    // Correct bit length?
    if (bitbuffer->bits_per_row[0] != 40) {
        if (bitbuffer->bits_per_row[0] != 0) {
            decoder_logf(decoder, 1, __func__, "wrong number of bits (%d)", bitbuffer->bits_per_row[0]);
        }
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[0];

    // No need to decode/extract values for simple test
    if (!b[0] && !b[2] && !b[3] && !b[4]) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0xff");
        return DECODE_FAIL_SANITY;
    }

    // Correct start sequence?
    if (b[0] != 0x00) {
        decoder_log(decoder, 1, __func__, "wrong start nibble");
        return DECODE_FAIL_SANITY;
    }

    // Correct checksum?
    if (xor_bytes(b, 5) && xor_bytes(b, 3) ^ b[4]) {
        decoder_log(decoder, 1, __func__, "bad checksum");
        return DECODE_FAIL_MIC;
    }

    // Channel
    int channel = (b[1]);
    switch (channel) {
    case 0x36:
        channel = 3;
        break;
    case 0x45:
        channel = 2;
        break;
    case 0x5A:
        channel = 1;
        break;
    default:
        channel = 0;
        break;
    }
    decoder_logf(decoder, 1, __func__, "channel decoded is %d", channel);

    // Temperature
    int temp_raw = ((b[3] & 0x3f) << 8) | b[2];
    float temp_c = (temp_raw / 353.0f) - 9.2f; // TODO: this is very likely wrong
    decoder_logf(decoder, 1, __func__, "temperature: raw: %d %08X converted: %.2f", temp_raw, temp_raw, temp_c);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Philips-AJ7010",
            "channel",          "Channel",      DATA_INT,    channel,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "time",
        "model",
        "channel",
        "temperature_C",
        "mic",
        NULL,
};

r_device const philips_aj7010 = {
        .name        = "Philips outdoor temperature sensor (type AJ7010)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 2000,
        .long_width  = 6000,
        .sync_width  = 1000,
        .reset_limit = 30000,
        .decode_fn   = &philips_aj7010_decode,
        .fields      = output_fields,
};
