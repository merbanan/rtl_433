/** @file
    Eurochron EFTH-800 temperature and humidity sensor.

    Copyright (c) 2020 by Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Eurochron EFTH-800 temperature and humidity sensor.

Wakeup of short pulse, 4x 970 us gap, 990 us pulse,
packet gap of 4900 us,
two packets of each
4x 750 us pulse, 720 us gap, then
(1-bit) 500 us pulse, 230 us gap or
(0-bit) 250 us pulse, 480 us gap.

There might be an alternative (longer) packet interleaved, e.g.:

    {65} 2B 1E A9 90 AB D3 83 2A 8
    {49} AB 1F B3 B7 B6 BE 80
    {65} 2B 1E A9 90 AB D3 83 2A 8
    {49} AB 1F B3 B7 B6 BE 8

Data layout:

    ?ccc iiii  iiii iiii  bntt tttt  tttt ????  hhhh hhhh  xxxx xxxx

- c:  3 bit channel valid channels are 0-7 (stands for channel 1-8)
- i: 12 bit random id (changes on power-loss)
- b:  1 bit battery indicator (0=>OK, 1=>LOW)
- n:  1 bit temperature sign? (0=>negative, 1=>positive)
- t: 10 bit signed temperature, scaled by 10
- h:  8 bit relative humidity percentage (BCD)
- x:  8 bit CRC-8, poly 0x31, init 0x00
- ?: unknown (Bit 0, 28-31 always 0 ?)

The sensor sends messages at intervals of about 57-58 seconds.
*/

#include "decoder.h"

static int eurochron_efth800_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    /* Look for clock packet */
    char dcf77_str[20] = {0}; // "2064-16-32T32:64:64"
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 65);
    if (row > 0) {
        uint8_t *b = bitbuffer->bb[row];

        // 0         1      2       3       4       5    6         7
        // ?1b CH:3d ID:12d 3b H?5d 2b M:6d 2b S:6d Y?7d D:5d M:4d CHK?8h 1x
        // TODO: (b[2] >> 5) may have DST and/or TZ info ?
        int dcf77_hour = (b[2] & 0x1f);
        int dcf77_min  = (b[3] & 0x3f);
        int dcf77_sec  = (b[4] & 0x3f);
        int dcf77_year = (b[5] >> 1);
        int dcf77_day  = ((b[5] & 0x01) << 4) | (b[6] & 0xf0) >> 4;
        int dcf77_mth  = (b[6] & 0x0f);

        if (!crc8(b, 8, 0x31, 0x00)) {
            snprintf(dcf77_str, sizeof(dcf77_str), "%4d-%02d-%02dT%02d:%02d:%02d", dcf77_year + 2000, dcf77_mth, dcf77_day, dcf77_hour, dcf77_min, dcf77_sec);
        }
    }

    // Remove long rows with unknown data
    for (row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] > 49) {
            bitbuffer->bits_per_row[row] = 0; // cancel row
        }
    }

    /* Validation checks */
    row = bitbuffer_find_repeated_row(bitbuffer, 2, 48);

    if (row < 0) // repeated rows?
        return DECODE_ABORT_EARLY;

    if (bitbuffer->bits_per_row[row] > 49) // 48 bits per row?
        return DECODE_ABORT_LENGTH;

    uint8_t *b = bitbuffer->bb[row];

    // No need to decode/extract values for simple test
    if (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x00 && b[4] == 0x00) {
        // data has been inverted at this point, originally it was 0xff
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0xff");
        return DECODE_FAIL_SANITY;
    }

    if (crc8(b, 6, 0x31, 0x00)) {
        return DECODE_FAIL_MIC; // crc mismatch
    }

    /* Extract data */
    int channel     = (b[0] & 0x70) >> 4;
    int id          = ((b[0] & 0x0f) << 8) | b[1];
    int battery_low = b[2] >> 7;
    int temp_raw    = (int16_t)((b[2] & 0x3f) << 10) | ((b[3] & 0xf0) << 2); // sign-extend
    float temp_c    = (temp_raw >> 6) * 0.1f;
    int humidity    = (b[4] >> 4) * 10 + (b[4] & 0xf); // BCD

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Eurochron-EFTH800",
            "id",               "",             DATA_INT,    id,
            "channel",          "",             DATA_INT,    channel + 1,
            "battery_ok",       "Battery",      DATA_INT,    !battery_low,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_INT,    humidity,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            "radio_clock",      "Radio Clock",  DATA_COND,   *dcf77_str, DATA_STRING, dcf77_str,
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
        "radio_clock",
        NULL,
};

r_device const eurochron_efth800 = {
        .name        = "Eurochron EFTH-800 temperature and humidity sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 250,
        .long_width  = 500,
        .sync_width  = 750,
        .gap_limit   = 900,
        .reset_limit = 5500,
        .decode_fn   = &eurochron_efth800_decode,
        .fields      = output_fields,
};
