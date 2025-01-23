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

Circuit board model numbers: TX07Y-THC V1, TX07K-THC V4

Similar to prologue, kedsum, esperanza_ews, s3318p
Only available information for this device: https://fcc.report/FCC-ID/WEC-2103

Data:

    Byte:      0        1        2        3        4        5
    Nibble:    1   2    3   4    5   6    7   8    9   10   11
    Type:      IIIIIIII XXXXFFFF TTTTTTTT TTTTHHHH HHHHCCCC SS

- I: random device ID, changes on powercycle
- X: Checksum: mangled CRC-4, poly 3, init 0.
- F: Flags: tx-button pressed|batt-low|?|?
- T: Temperature
- H: Humidity
- S: Stop bit(s): 0b10

Example datagram:

     f2 90             6b5         96       1       8
    |ID|Checksum+Flags|Temperature|Humidity|Channel|Stop bits

- Temperature in Fahrenheit*100+900->hex
- Example: 82.4F->824->1724->0x6bc
*/

static int wec2103_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 6 || bitbuffer->bits_per_row[2] != 42) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[5];
    bitbuffer_extract_bytes(bitbuffer, 3, 0, b, 40);

    int crc_received = b[1] >> 4;
    b[1] = (b[1] & 0x0F) | ((b[4] & 0x0f) << 4);
    int crc_calculated = crc4(b, sizeof(b) - 1, 3, 0) ^ (b[4] >> 4);
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
    data_t *data = NULL;
    data = data_str(data, "model",            "",             NULL,         "WEC-2103");
    data = data_int(data, "id",               "ID",           NULL,         device_id);
    data = data_int(data, "channel",          "Channel",      NULL,         channel);
    data = data_int(data, "battery_ok",       "Battery",      NULL,         !battery_low);
    data = data_int(data, "button",           "Button",       NULL,         button);
    data = data_dbl(data, "temperature_F",    "Temperature",  "%.2f F",     temp_f);
    data = data_int(data, "humidity",         "Humidity",     "%u %%",      humidity);
    data = data_int(data, "flags",            "Flags",        NULL,         flags);
    data = data_str(data, "mic",              "Integrity",    NULL,         "CRC");
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
        "temperature_F",
        "humidity",
        "flags",
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
