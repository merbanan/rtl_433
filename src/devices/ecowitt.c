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

|  0-6  | 7-bit header, ignored for checksum, always 1111111
|  7-14 | Always 01010011
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
    // All Ecowitt packets have one row.
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_LENGTH;
    }

    // All Ecowitt packets have 55 bits.
    if (bitbuffer->bits_per_row[0] != 55) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *row = bitbuffer->bb[0];

    // All Ecowitt packets have the first 7 bits set.
    uint8_t first7bits = row[0] >> 1;
    if (first7bits != 0x7F) {
        return DECODE_ABORT_EARLY;
    }

    // Byte-align the rest of the message by skipping the first 7 bits.
    uint8_t b[6];
    bitbuffer_extract_bytes(bitbuffer, /* row= */ 0, /* pos= */ 7, b, sizeof(b) * 8); // Skip first 7 bits

    // All Ecowitt packets continue with a fixed header
    if (b[0] != 0x53) {
        return DECODE_ABORT_EARLY;
    }

    // Randomly generated at boot time sensor ID.
    int sensor_id = b[1];

    int channel = b[2] >> 4; // First nybble.
    channel++; // Convert 0-indexed wire protocol to 1-indexed channels on the device UI
    if (channel < 1 || channel > 3) {
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
    float temp_c = -40.0; // Bias
    temp_c += (float)b[3] / 10.0;
    temp_c += (float)((b[2] & 0x3) << 8) / 10.0;

    // All Ecowitt observed packets have bits 39-48 set.
    if (b[4] != 0xFF) {
        return DECODE_ABORT_EARLY;
    }

    // Compute checksum skipping first 7 bits
    uint8_t wire_crc = b[5];
    int computed_crc = crc8(
            b,
            /* nBytes= */ sizeof(b) - 1, // Exclude the CRC byte itself
            /* polynomial= */ 0x31,
            /* init= */ 0);
    if (wire_crc != computed_crc) {
        return DECODE_FAIL_MIC;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model", "", DATA_STRING, "Ecowitt-WH53",
            "id", "Id", DATA_INT, sensor_id,
            "channel", "Channel", DATA_INT, channel,
            "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "mic", "Integrity", DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "temperature_C",
        "mic",
        NULL,
};

r_device ecowitt = {
        .name        = "Ecowitt Wireless Outdoor Thermometer WH53/WH0280/WH0281A",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 500,           // 500 us nominal short pulse
        .long_width  = 1480,          // 1480 us nominal long pulse
        .gap_limit   = 1500,          // 960 us nominal fixed gap
        .reset_limit = 2000,          // 31 ms packet distance (too far apart)
        .sync_width  = 0,
        .decode_fn   = &ecowitt_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
