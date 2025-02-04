/** @file
    Geevon TX16-3 Remote Outdoor Sensor with LCD Display.

    Contributed by Matt Falcon <falcon4@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdbool.h>
#include "decoder.h"

/**
Geevon TX16-3 and TX19-1 Remote Outdoor Sensor with LCD Display.

These devices are a simple temperature/humidity transmitter, TX16-3 has a small LCD display for local viewing.

The test packet represents:
- channel 1
- battery OK
- temperature of 62.6 Fahrenheit or 17 Celsius
- 43% relative humidity.

Data layout for TX16-3:

    Byte 0   Byte 1   Byte 2   Byte 3   Byte 4   Byte 5   Byte 6   Byte 7   Byte 8
    IIIIIIII BxCCxxxx TTTTTTTT TTTT0000 HHHHHHHH FFFFFFFF FFFFFFFF FFFFFFFF CCCCCCCC
       87       00       29       e0       2b       aa       55       aa       e8

Data layout for TX19-1:
    Byte 0   Byte 1   Byte 2   Byte 3   Byte 4   Byte 5   Byte 6   Byte 7   Byte 8   Byte 9
    IIIIIIII BxCCxxxx TTTTTTTT TTTT0000 HHHHHHHH FFFFFFFF FFFFFFFF FFFFFFFF CCCCCCCC M
       b5       00       2d       40       2c       aa       55       aa       af

- I: ID?
- B: Battery low status (0 = good, 1 = low battery)
- C: Channel (0, 1, 2 as channels 1, 2, 3)
- T: Temperature - represented as ((degrees C * 10) + 500)
- H: Relative humidity - represented as percentage %
- F: Integrity check - 3 bytes are always 0xAA 0x55 0xAA
- X: CRC checksum (CRC-8 poly 0x31 init=0x7b)
- M: set if it's a middle packet (3rd out of 5)

Format string:

    ID:8h BATT:b ?:b CHAN:2h FLAGS:4h TEMP_C:12d PAD:4h HUM:8d FIX:24h CRC:8h 1x

Example packets for TX16-3:

    f4002ac039aa55aa11
    f4002ab039aa55aa54
    f4002aa039aa55aa28
    f4002a9039aa55aaac

Example packets for TX19-1 (in the last nibble only the most significant bit is used)

    b5002d402caa55aaf20
    b5002d402caa55aaf28
    01102d402daa55aa130
    01102d402eaa55aaea0
    01002d302daa55aaf20
    01002d302daa55aaf20
    01202d302daa55aa8b0

*/

enum checksum_type {
    CRC8,
    LFSR,
};

static int geevon_decode(r_device *decoder, bitbuffer_t *bitbuffer,
                                   enum checksum_type checksum_type)
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

    // Verify checksum
    switch (checksum_type) {
        case CRC8:
            if (crc8(b, 9, 0x31, 0x7b)) {
                return DECODE_FAIL_MIC;
            }
            break;
        case LFSR:
            if (b[8] != lfsr_digest8_reverse(b, 8, 0x98, 0x25)) {
                return DECODE_FAIL_MIC;
            }
            break;
        default:
            break;
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
            "model",            "",             DATA_COND, checksum_type == CRC8,  DATA_STRING, "Geevon-TX163",
            "model",            "",             DATA_COND, checksum_type == LFSR,  DATA_STRING, "Geevon-TX191",
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

/** @sa geevon_decode() */
static int geevon_tx16_3_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    return geevon_decode(decoder, bitbuffer, CRC8);
}

/** @sa geevon_decode() */
static int geevon_tx19_1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    return geevon_decode(decoder, bitbuffer, LFSR);
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

r_device const geevon_tx16_3 = {
        .name        = "Geevon TX16-3 outdoor sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 250,
        .long_width  = 500,
        .sync_width  = 750,  // sync pulse is 728 us + 728 us gap
        .gap_limit   = 625,  // long gap (with short pulse) is ~472 us, sync gap is ~728 us
        .reset_limit = 1700, // maximum gap is 1250 us (long gap + longer sync gap on last repeat)
        .decode_fn   = &geevon_tx16_3_decode,
        .fields      = output_fields,
};

r_device const geevon_tx19_1 = {
        .name        = "Geevon TX19-1 outdoor sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 250,
        .long_width  = 500,
        .sync_width  = 750,  // sync pulse is ~728 us + ~728 us gap
        .gap_limit   = 625,  // long gap (with short pulse) is ~472 us, sync gap is ~728 us
        .reset_limit = 1700, // maximum gap is 1250 us (long gap + longer sync gap on last repeat)
        .decode_fn   = &geevon_tx19_1_decode,
        .fields      = output_fields,
};
