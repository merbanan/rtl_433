/** @file
    Geevon TX16-3 Remote Outdoor Sensor with LCD Display.

    Contributed by Matt Falcon <falcon4@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Geevon TX16-3 Remote Outdoor Sensor with LCD Display.

This device is a simple temperature/humidity transmitter with a small LCD display for local viewing.

The test packet represents:
- channel 1
- battery OK
- temperature of 62.6 Fahrenheit or 17 Celsius
- 43% relative humidity.

Data layout:

    Byte 0   Byte 1   Byte 2   Byte 3   Byte 4   Byte 5   Byte 6   Byte 7   Byte 8
    IIIIIIII BxCCxxxx TTTTTTTT TTTT0000 HHHHHHHH FFFFFFFF FFFFFFFF FFFFFFFF CCCCCCCC
       87       00       29       e0       2b       aa       55       aa       e8

- I: ID?
- B: Battery low status (0 = good, 1 = low battery)
- C: Channel (0, 1, 2 as channels 1, 2, 3)
- T: Temperature - represented as ((degrees C * 10) + 500)
- H: Relative humidity - represented as percentage %
- F: Integrity check - 3 bytes are always 0xAA 0x55 0xAA
- X: CRC checksum (CRC-8 poly 0x31 init=0x7b)

Format string:

    ID:8h BATT:b ?:b CHAN:2h FLAGS:4h TEMP_C:12d PAD:4h HUM:8d FIX:24h CRC:8h 1x

Example packets:

    f4002ac039aa55aa11
    f4002ab039aa55aa54
    f4002aa039aa55aa28
    f4002a9039aa55aaac

*/

static int geevon_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // invert all the bits
    bitbuffer_invert(bitbuffer);

    // find the most common row, nominal we expect 5 packets
    int r = bitbuffer_find_repeated_prefix(bitbuffer, bitbuffer->num_rows > 5 ? 5 : 3, 72);
    if (r < 0) {
        return DECODE_ABORT_LENGTH;
    }

    // work with the best/most repeated capture
    uint8_t *b = bitbuffer->bb[r];

    // Check if the packet has the correct number of bits
    if (bitbuffer->bits_per_row[r] != 73) {
        return DECODE_ABORT_LENGTH;
    }

    // Check if the fixed bytes are correct
    if (b[5] != 0xAA || b[6] != 0x55 || b[7] != 0xAA) {
        return DECODE_FAIL_MIC;
    }

    // Verify CRC checksum
    uint8_t chk = crc8(b, 9, 0x31, 0x7b);
    if (chk) {
        return DECODE_FAIL_MIC;
    }

    // Extract the data from the packet
    int battery_low = (b[1] >> 7);              // 0x00: battery good, 0x80: battery low
    int channel     = ((b[1] & 0x30) >> 4) + 1; // channel: 1, 2, 3
    int temp_raw    = (b[2] << 4) | (b[3] >> 4);
    float temp_c    = (temp_raw - 500) * 0.1f; // temperature is ((degrees c + 500) * 10)
    int humidity    = b[4];

    // Store the decoded data
    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Geevon-TX163",
            "id",               "",             DATA_INT,    b[0],
            "battery_ok",       "Battery",      DATA_INT,    !battery_low,
            "channel",          "Channel",      DATA_INT,    channel,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT,     humidity,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "battery",
        "channel",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device const geevon = {
        .name        = "Geevon TX16-3 outdoor sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 250,
        .long_width  = 500,
        .sync_width  = 750,  // sync pulse is 728 us + 728 us gap
        .gap_limit   = 625,  // long gap (with short pulse) is ~472 us, sync gap is ~728 us
        .reset_limit = 1700, // maximum gap is 1250 us (long gap + longer sync gap on last repeat)
        .decode_fn   = &geevon_callback,
        .fields      = output_fields,
};
