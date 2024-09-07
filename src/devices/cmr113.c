/** @file
    Clipsal CMR113 cent-a-meter power meter.

    Copyright (C) 2021 Michael Neuling <mikey@neuling.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Clipsal CMR113 cent-a-meter power meter.

The demodulation comes in a few stages:

A) Firstly we look at the pulse lengths both high and low. These
   are demodulated using OOK_PULSE_PIWM_DC before we hit this
   driver. Any short pulse (high or low) is assigned a 1 and a
   long pulse (high or low) is assigned a 0. ie every pulse is a
   bit.

B) We then look for two patterns in this new bitstream:
    - 0b00 (ie long long from stream A)
    - 0b011 (ie long short short from stream A)

C) We start off with an output bit of '0'.  When we see a 0b00
   (from B), the next output bit is the same as the last
   bit. When we see a 0b011 (from B), the next output is
   toggled. If we don't see ether of these patterns, we fail.

D) The output from C represents the final bitstream. This is 83
   bits repeated twice. There are some timestamps, transmitter
   IDs and CRC but all we decode below are the 3 current values
   which are 10 bits each representing AMPS/10. We do check the
   two 83 bit are identical and fail if not.

Kudos to Jon Oxer for decoding this stream and putting it here:
https://github.com/jonoxer/CentAReceiver

*/

#define COMPARE_BITS  83
#define COMPARE_BYTES ((COMPARE_BITS + 7) / 8)

static int cmr113_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int start, bit;
    uint8_t buf[4];
    uint8_t b1[COMPARE_BYTES], b2[COMPARE_BYTES];
    bitbuffer_t b = {0};
    double current[3];
    data_t *data;

    if ((bitbuffer->bits_per_row[0] < 350) || (bitbuffer->bits_per_row[0] > 450))
        return DECODE_ABORT_LENGTH;

    bitbuffer_extract_bytes(bitbuffer, 0, 0, buf, 32);
    if ((buf[0] != 0xb0) || (buf[1] != 0x00) || (buf[2] != 0x00))
        return DECODE_ABORT_EARLY;

    start = 0;
    bit = 0;
    bitbuffer_clear(&b);
    while ((start + 3) < bitbuffer->bits_per_row[0]) {
        bitbuffer_extract_bytes(bitbuffer, 0, start, buf, 3);
        if ((buf[0] >> 6) == 0x00) { // top two bits are 0b00 = no toggle
            start += 2;
            bitbuffer_add_bit(&b, bit);
        } else if ((buf[0] >> 5) == 0x03) { // top two bits are 0b011 = toggle
            start += 3;
            bit = 1 - bit; // toggle
            bitbuffer_add_bit(&b, bit);
        } else if (start == 0)
            start += 1; // first bit doesn't decode
        else
            // we don't have enough bits
            return DECODE_ABORT_LENGTH;
    }

    if (b.bits_per_row[0] < 2 * COMPARE_BITS + 2)
        return DECODE_ABORT_LENGTH;

    // Compare the repeated section to ensure data integrity
    bitbuffer_extract_bytes(&b, 0, 0, b1, COMPARE_BITS);
    bitbuffer_extract_bytes(&b, 0, COMPARE_BITS + 2, b2, COMPARE_BITS);
    if (memcmp(b1, b2, COMPARE_BYTES) != 0)
        return DECODE_FAIL_MIC;

    // Data is all good, so extract 3 phases of current
    for (int i = 0; i < 3; i++) {
        bitbuffer_extract_bytes(&b, 0, 36 + i * 10, buf, 10);
        reflect_bytes(buf, 2);
        current[i] = ((float)buf[0] + ((buf[1] & 0x3) << 8)) * 0.1;
    }

    /* clang-format off */
    data = data_make(
            "model",        "",             DATA_STRING, "Clipsal-CMR113",
            "current_1_A",  "Current 1",    DATA_FORMAT, "%.1f A", DATA_DOUBLE, current[0],
            "current_2_A",  "Current 2",    DATA_FORMAT, "%.1f A", DATA_DOUBLE, current[1],
            "current_3_A",  "Current 3",    DATA_FORMAT, "%.1f A", DATA_DOUBLE, current[2],
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "current_1_A",
        "current_2_A",
        "current_3_A",
        NULL,
};

// Short high and low pulses are quite different in length so we have a high tolerance of 200
r_device const cmr113 = {
        .name        = "Clipsal CMR113 Cent-a-meter power meter",
        .modulation  = OOK_PULSE_PIWM_DC,
        .short_width = 480,
        .long_width  = 976,
        .sync_width  = 2028,
        .reset_limit = 2069,
        .tolerance   = 200,
        .decode_fn   = &cmr113_decode,
        .fields      = output_fields,
};
