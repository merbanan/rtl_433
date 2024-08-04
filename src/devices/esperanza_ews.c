/** @file
    Esperanza EWS-103 sensor on 433.92Mhz.

    Copyright (C) 2015 Alberts Saulitis
    Enhanced (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Largely the same as kedsum, s3318p.
@sa kedsum.c s3318p.c

List of known supported devices:
- JYWDJ-009
      * Known voltage operating range 1.7V - 3.8V
      * Low-batt flag is raised when supply voltage goes below 2.75V

Frame structure:

    Byte:      0        1        2        3        4
    Nibble:    1   2    3   4    5   6    7   8    9   10
    Type:   00 IIIIIIII ??CCTTTT TTTTTTTT HHHHHHHH FFFFXXXX

- 0: Preamble
- I: Random device ID
- C: Channel (1-3)
- T: Temperature (Little-endian)
- H: Humidity (Little-endian)
- F: Flags (unknown low-batt unknown unknown)
- X: CRC-4 poly 0x3 init 0x0 xor last 4 bits

Flags (bbbb)
3: Unknown
2: low-batt Flag is raised when supply voltage drops below threshold.
1: Unknown
0: Unknown

Sample Data:

    Esperanze EWS: TemperatureF=55.5 TemperatureC=13.1 Humidity=74 Device_id=0 Channel=1

    bitbuffer:: Number of rows: 14
    [00] {0} :
    [01] {0} :
    [02] {42} 00 53 e5 69 02 00 : 00000000 01010011 11100101 01101001 00000010 00
    [03] {0} :
    [04] {42} 00 53 e5 69 02 00 : 00000000 01010011 11100101 01101001 00000010 00
    [05] {0} :
    [06] {42} 00 53 e5 69 02 00 : 00000000 01010011 11100101 01101001 00000010 00
    [07] {0} :
    [08] {42} 00 53 e5 69 02 00 : 00000000 01010011 11100101 01101001 00000010 00
    [09] {0} :
    [10] {42} 00 53 e5 69 02 00 : 00000000 01010011 11100101 01101001 00000010 00
    [11] {0} :
    [12] {42} 00 53 e5 69 02 00 : 00000000 01010011 11100101 01101001 00000010 00
    [13] {0} :
*/

#include "decoder.h"

static int esperanza_ews_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t b[5];
    data_t *data;

    // require two leading sync pulses
    if (bitbuffer->bits_per_row[0] != 0 || bitbuffer->bits_per_row[1] != 0)
        return DECODE_FAIL_SANITY;

    if (bitbuffer->num_rows != 14)
        return DECODE_ABORT_LENGTH;

    for (int row = 2; row < bitbuffer->num_rows - 3; row += 2) {
        if (memcmp(bitbuffer->bb[row], bitbuffer->bb[row + 2], sizeof(bitbuffer->bb[row])) != 0
                || bitbuffer->bits_per_row[row] != 42)
            return DECODE_FAIL_SANITY;
    }
    int r = 2;

    // remove the two leading 0-bits and align the data
    bitbuffer_extract_bytes(bitbuffer, r, 2, b, 40);

    // CRC-4 poly 0x3, init 0x0 over 32 bits then XOR the next 4 bits
    int crc = crc4(b, 4, 0x3, 0x0) ^ (b[4] >> 4);
    if (crc != (b[4] & 0xf))
        return DECODE_FAIL_MIC;

    int device_id = b[0];
    int channel   = ((b[1] & 0x30) >> 4) + 1;
    // Battery status is the 7th bit 0x40. 0 = normal, 1 = low
    unsigned char const battery_low = (b[4] & 0x40) == 0x40;
    int temp_raw  = ((b[2] & 0x0f) << 8) | (b[2] & 0xf0) | (b[1] & 0x0f);
    float temp_f  = (temp_raw - 900) * 0.1f;
    int humidity  = ((b[3] & 0x0f) << 4) | ((b[3] & 0xf0) >> 4);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Esperanza-EWS",
            "id",               "ID",           DATA_INT, device_id,
            "channel",          "Channel",      DATA_INT, channel,
            "battery_ok",       "Battery",      DATA_INT, !battery_low,
            "temperature_F",    "Temperature",  DATA_FORMAT, "%.2f F", DATA_DOUBLE, temp_f,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
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
        "temperature_F",
        "humidity",
        "mic",
        NULL,
};

r_device const esperanza_ews = {
        .name        = "Esperanza EWS",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .gap_limit   = 4400,
        .reset_limit = 9400,
        .decode_fn   = &esperanza_ews_callback,
        .fields      = output_fields,
};
