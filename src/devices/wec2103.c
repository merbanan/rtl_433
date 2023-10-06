/** @file
    WEC-2103 temperature/humidity sensor.

    Copyright (C) 2022 Tobias Thurnreiter

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

#include "decoder.h"

/**
WEC-2103 temperature/humidity sensor.

Similar to prologue, kedsum, esperanza_ews, s3318p
Only available information for this device: https://fcc.report/FCC-ID/WEC-2103

Data:

    Byte:      0        1        2        3        4        5
    Nibble:    1   2    3   4    5   6    7   8    9   10   11
    Type:      IIIIIIII XXXXFFFF TTTTTTTT TTTTHHHH HHHHCCCC ????

- I: random device ID, changes on powercycle
- X: Checksum?
- F: Flags
- T: Temperature
- H: Humidity
- Flags: tx-button pressed|batt-low|?|?

Example datagram:

     f2 90              6b5         96       1       8
    |ID|Checksum?+Flags|Temperature|Humidity|Channel|unknown

- Temperature in Fahrenheit*100+900->hex
- Example: 82.4F->824->1724->0x6bc
*/

static uint8_t almost_crc4(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init)
{
    unsigned remainder = init;
    unsigned poly = polynomial;
    unsigned bit;

    while (nBytes--) {
        // In normal CRC, the XOR message goes here.
        for (bit = 0; bit < 4; bit++) {
            if (remainder & 0x08) {
                remainder = (remainder << 1) ^ poly;
            } else {
                remainder = (remainder << 1);
            }
        }
        remainder ^= *message++;
    }
    return remainder & 0x0f;
}

static int wec2103_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 6 || bitbuffer->bits_per_row[2] != 42) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[5];
    bitbuffer_extract_bytes(bitbuffer, 3, 0, b, 40);

    uint8_t c[9];
    c[0] = b[0] >> 4;
    c[1] = b[0] & 0x0F;
    c[2] = b[4] & 0x0F; // The last nibble is moved here.
    c[3] = b[1] & 0x0F;
    c[4] = b[2] >> 4;
    c[5] = b[2] & 0x0F;
    c[6] = b[3] >> 4;
    c[7] = b[3] & 0x0F;
    c[8] = b[4] >> 4;

    int crc_calculated = almost_crc4(c, sizeof(c), 3, 0);
    int crc_received = b[1] >> 4;
    if (crc_calculated != crc_received) {
        decoder_logf(decoder, 0, __func__, "CRC check failed (0x%X != 0x%X)", crc_calculated, crc_received);
        return DECODE_FAIL_MIC;
    }

    int temp_raw    = (b[2] << 4) | ((b[3] & 0xf0) >> 4);
    int device_id   = b[0];
    int channel     = b[4] & 0x0f;
    int flags       = b[1] & 0xf;
    float temp_f    = (temp_raw - 900) * 0.1f;
    int humidity    = ((b[3] & 0x0f) * 10) + ((b[4] & 0xf0) >> 4);
    int button      = (b[1] & 0x08) >> 3;
    int battery_low = (b[1] & 0x04) >> 3;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "WEC-2103",
            "id",               "ID",           DATA_INT,    device_id,
            "channel",          "Channel",      DATA_INT,    channel,
            "flags",            "Flags",        DATA_INT,    flags,
            "temperature_F",    "Temperature",  DATA_FORMAT, "%.02f F", DATA_DOUBLE, temp_f,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "battery_ok",       "Battery",      DATA_INT,    !battery_low,
            "button",           "Button",       DATA_INT,    button,
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
        "flags",
        "temperature_F",
        "humidity",
        "battery_low",
        "button",
        "mic",
        NULL,
};

r_device const wec2103 = {
        .name           = "WEC-2103 temperature/humidity sensor",
        .modulation     = OOK_PULSE_PPM,
        .short_width    = 1900,
        .long_width     = 3800,
        .gap_limit      = 4400,
        .reset_limit    = 9400,
        .decode_fn      = &wec2103_decode,
        .fields         = output_fields,
};
