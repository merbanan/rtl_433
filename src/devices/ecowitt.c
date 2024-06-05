/** @file
    Ecowitt Wireless Outdoor Thermometer WH53/WH0280/WH0281A.

    Copyright 2019 Google LLC.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Ecowitt Wireless Outdoor Thermometer WH53/WH0280/WH0281A.

55-bit one-row data packet format (inclusive ranges, 0-indexed):

|  0-6  | 7-bit header, ignored for checksum, always 1111111, not stable, could be 6 x 1 bit see #2933
|  7-14 | Model code, 0x53
| 15-22 | Sensor ID, randomly reinitialized on boot
| 23-24 | Always 00
| 25-26 | 2-bit sensor channel, selectable on back of sensor {00=1, 01=2, 10=3}
| 27-28 | Always 00
| 29-38 | 10-bit temperature in tenths of degrees C, starting from -40C. e.g. 0=-40C
| 39-46 | Trailer, always 1111 1111
| 47-54 | CRC-8 checksum poly 0x31 init 0x00 skipping first 7 bits
*/

#include "decoder.h"

static int ecowitt_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {
        0xf5, 0x30  // preamble and model code nominally 7+8 bit, look for 12 bit only #2933
        };

    // All Ecowitt packets have one row.
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_LENGTH;
    }

    unsigned pos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 12);

    // Preamble found ?
    if (pos >= bitbuffer->bits_per_row[0]) {
        decoder_logf(decoder, 2, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }

    // 4 + 6*8 bit required
    if ((bitbuffer->bits_per_row[0] - pos) < 52) {
        decoder_logf(decoder, 2, __func__, "Too short");
        return DECODE_ABORT_EARLY;
    }

    // Byte-align the rest of the message by skipping the first 4 bit.
    uint8_t b[6];
    bitbuffer_extract_bytes(bitbuffer, 0, pos + 4 , b, sizeof(b) * 8); // Skip first 4 bit but keep model 0x53 needed for crc
    decoder_log_bitrow(decoder, 2, __func__, b, sizeof(b) * 8, "MSG");

    // check crc, poly 0x31, init 0x00
    if (crc8(b, 6, 0x31, 0)) {
        return DECODE_FAIL_MIC;
    }

    // Randomly generated at boot time sensor ID.
    int sensor_id = b[1];

    int channel = b[2] >> 4; // First nybble.
    channel++; // Convert 0-indexed wire protocol to 1-indexed channels on the device UI
    if (channel > 3) {
        return DECODE_FAIL_SANITY; // The switch only has 1-3.
    }

    // All Ecowitt packets have bits 27 and 28 set to 0
    // Perhaps these are just an extra two high bits for temperature?
    // The manual though says it only operates to 60C, which about matches 10 bits (1023/10-40C)=62.3C
    // Above 60 is pretty hot - let's just check these are always zero.
    if ((b[2] & (4 | 8)) != 0) {
        return DECODE_ABORT_EARLY;
    }

    // Temperature is next 10 bits
    int temp_raw = ((b[2] & 0x3) << 8) | b[3];
    float temp_c = (temp_raw - 400) * 0.1f;

    // All Ecowitt observed packets have bits 39-48 set.
    if (b[4] != 0xFF) {
        return DECODE_ABORT_EARLY;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",            DATA_STRING, "Ecowitt-WH53",
            "id",            "Id",          DATA_INT,    sensor_id,
            "channel",       "Channel",     DATA_INT,    channel,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "mic",           "Integrity",   DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "temperature_C",
        "mic",
        NULL,
};

r_device const ecowitt = {
        .name        = "Ecowitt Wireless Outdoor Thermometer WH53/WH0280/WH0281A",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 500,  // 500 us nominal short pulse
        .long_width  = 1480, // 1480 us nominal long pulse
        .gap_limit   = 1500, // 960 us nominal fixed gap
        .reset_limit = 2000, // 31 ms packet distance (too far apart)
        .sync_width  = 0,
        .decode_fn   = &ecowitt_decode,
        .fields      = output_fields,
};
