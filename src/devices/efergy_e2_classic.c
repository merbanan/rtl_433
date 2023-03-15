/** @file
    Efergy e2 classic (electricity meter).

    Copyright (C) 2015 Robert Högberg <robert.hogberg@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Efergy e2 classic (electricity meter).

This electricity meter periodically reports current power consumption
on frequency ~433.55 MHz. The data that is transmitted consists of 8
bytes:

- Byte 1: Start bits (00)
- Byte 2-3: Device id
- Byte 4: Learn mode, sending interval and battery status
- Byte 5-7: Current power consumption
  -  Byte 5: Integer value (High byte)
  -  Byte 6: integer value (Low byte)
  -  Byte 7: exponent (values between -3? and 4?)
- Byte 8: Checksum

Power calculations come from Nathaniel Elijah's program EfergyRPI_001.

Test codes:
- Current   4.64 A: {65}0cc055604a41030f8
- Current 185.16 A: {65}0cc055605c9408798

*/

#include "decoder.h"

static int efergy_e2_classic_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    unsigned num_bits = bitbuffer->bits_per_row[0];
    uint8_t *bytes = bitbuffer->bb[0];
    data_t *data;

    if (num_bits < 64 || num_bits > 80) {
        return DECODE_ABORT_LENGTH;
    }

    // The bit buffer isn't always aligned to the transmitted data, so
    // search for data start and shift out the bits which aren't part
    // of the data. The data always starts with 0000 (or 1111 if
    // gaps/pulses are mixed up).
    while ((bytes[0] & 0xf0) != 0xf0 && (bytes[0] & 0xf0) != 0x00) {
        num_bits -= 1;
        if (num_bits < 64) {
            return DECODE_FAIL_SANITY;
        }

        for (unsigned i = 0; i < (num_bits + 7) / 8; ++i) {
            bytes[i] <<= 1;
            bytes[i] |= (bytes[i + 1] & 0x80) >> 7;
        }
    }

    // Sometimes pulses and gaps are mixed up. If this happens, invert
    // all bytes to get correct interpretation.
    if (bytes[0] & 0xf0) {
        for (unsigned i = 0; i < 8; ++i) {
            bytes[i] = ~bytes[i];
        }
    }

    int zero_count = 0;
    for (int i = 0; i < 8; i++) {
        if (bytes[i] == 0)
            zero_count++;
    }
    if (zero_count++ > 5)
        return DECODE_FAIL_SANITY; // too many Null bytes

    unsigned checksum = add_bytes(bytes, 7);

    if (checksum == 0) {
        return DECODE_FAIL_SANITY; // reduce false positives
    }
    if ((checksum & 0xff) != bytes[7]) {
        return DECODE_FAIL_MIC;
    }

    uint16_t address = bytes[2] << 8 | bytes[1];
    uint8_t learn    = (bytes[3] & 0x80) >> 7;
    uint8_t interval = (((bytes[3] & 0x30) >> 4) + 1) * 6;
    uint8_t battery  = (bytes[3] & 0x40) >> 6;
    uint8_t fact     = -(int8_t)bytes[6] + 15;
    if (fact < 7 || fact > 20) // full range unknown so far
        return DECODE_FAIL_SANITY; // invalid exponent
    float current_adc = (float)(bytes[4] << 8 | bytes[5]) / (1 << fact);

    /* clang-format off */
    data = data_make(
            "model",        "",                 DATA_STRING, "Efergy-e2CT",
            "id",           "Transmitter ID",   DATA_INT,    address,
            "battery_ok",   "Battery",          DATA_INT,    !!battery,
            "current",      "Current",          DATA_FORMAT, "%.2f A", DATA_DOUBLE, current_adc,
            "interval",     "Interval",         DATA_FORMAT, "%ds", DATA_INT, interval,
            "learn",        "Learning",         DATA_STRING, learn ? "YES" : "NO",
            "mic",          "Integrity",        DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "current",
        "interval",
        "learn",
        "mic",
        NULL,
};

r_device const efergy_e2_classic = {
        .name        = "Efergy e2 classic",
        .modulation  = FSK_PULSE_PWM,
        .short_width = 64,
        .long_width  = 136,
        .sync_width  = 500,
        .gap_limit   = 200,
        .reset_limit = 400,
        .decode_fn   = &efergy_e2_classic_callback,
        .fields      = output_fields,
};
