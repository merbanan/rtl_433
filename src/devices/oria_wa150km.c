/** @file
    Oria WA150KM temperature sensor decoder.

    Copyright (C) 2025 Jan Niklaas Wechselberg

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Oria WA150KM temperature sensor decoder.

The device uses Manchester coding with G.E. Thomas convention.
The data is bit-reflected.

Data layout after decoding:

    0  1  2  3  4  5  6  7  8  9  10 11 12 13
    FF FF FF MM ?? CC DD TT II SS ?? CH ?? BB

- FF = Preamble: 3 bytes of 0xff
- MM = Message type (unused)
- CC = Channel (upper nibble + 1)
- DD = Device ID
- TT = Temperature decimal (upper nibble)
- II = Temperature integer (BCD)
- SS = Sign bit (bit 4, 1 = negative)
- CH = Checksum (see below)
- BB = Fixed value 0x65

Observations currently not affecting implemetation:
- In normal operation, the MSG_TYPE toggles between fa20 and fa28 with every send (interval is ~34 seconds)
- Forced transmissions with the TX button have a MSG_TYPE=fa21
- DEVICE_IDs stay consistent over powercycles
- The devices transmit a "battery low" signal encoded in the byte after the temperature
- Negative temperatures have another single bit set

Checksum (issue #3136, reported against the shipped decoder's lack of
one): an Oregon Scientific v3 style nibble checksum, computed on the
Manchester-decoded bytes *before* reflect_bytes(). Reading nibbles
7..23 of that pre-reflected buffer (nibble 0 = byte 0's high nibble),
each individually 4-bit-reflected: the sum of the first 15 (mod 256)
must equal the last two combined as a byte (nibble 15 as the low
nibble, nibble 16 as the high nibble) -- byte 11 in the layout above.
Verified against 39 independently-decoded real frames from two
attachments in the issue thread (26 distinct readings, see
rtl_433_tests/tests/oria_wa150km/): matches on all 39, and every
single-bit corruption within the checked nibble range (but not outside
it) breaks the match.
*/

#define ORIA_WA150KM_BITLEN  227

// 4 bit nibble at nibble-index k (0 = byte 0's high nibble) of a byte array.
static uint8_t oria_nibble(uint8_t const *m, int k)
{
    uint8_t byte = m[k / 2];
    return (k % 2 == 0) ? (byte >> 4) & 0x0f : byte & 0x0f;
}

static uint8_t oria_reflect4(uint8_t n)
{
    return ((n & 0x1) << 3) | ((n & 0x2) << 1) | ((n & 0x4) >> 1) | ((n & 0x8) >> 3);
}

static int oria_wa150km_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Find a valid row (skipping short preamble rows)
    int r;
    for (r = 0; r < bitbuffer->num_rows; r++) {
        if (bitbuffer->bits_per_row[r] == ORIA_WA150KM_BITLEN) {
            break;
        }
    }
    if (r >= bitbuffer->num_rows) {
        decoder_logf(decoder, 2, __func__, "No valid row found with %d bits", ORIA_WA150KM_BITLEN);
        return DECODE_ABORT_LENGTH;
    }

    // Check warmup bytes before decoding
    uint8_t *b = bitbuffer->bb[r];
    if (b[0] != 0xAA || b[1] != 0xAA || b[2] != 0xAA) { // Check for alternating 1/0 pattern before Manchester decoding
        decoder_log(decoder, 2, __func__, "Warmup bytes are not 0xaaaaaa");
        return DECODE_ABORT_EARLY;
    }

    // Check last byte (raw data before Manchester decoding)
    if (b[bitbuffer->bits_per_row[r]/8 - 1] != 0x69) {
        decoder_log(decoder, 2, __func__, "Last byte is not 0x69");
        return DECODE_ABORT_EARLY;
    }

    // Invert the buffer for G.E. Thomas decoding
    bitbuffer_invert(bitbuffer);

    // Manchester decode the row
    bitbuffer_t manchester_buffer = {0};
    bitbuffer_manchester_decode(bitbuffer, r, 0, &manchester_buffer, ORIA_WA150KM_BITLEN);

    // Checksum: Oregon Scientific v3 style nibble checksum, computed
    // before reflect_bytes() (see file doc comment).
    uint8_t *m = manchester_buffer.bb[0];
    int sum = 0;
    for (int i = 0; i < 15; i++) {
        sum += oria_reflect4(oria_nibble(m, 7 + i));
    }
    uint8_t chk_calc = sum & 0xff;
    uint8_t chk_recv = oria_reflect4(oria_nibble(m, 22)) | (oria_reflect4(oria_nibble(m, 23)) << 4);
    if (chk_calc != chk_recv) {
        decoder_logf(decoder, 1, __func__, "Checksum invalid %02x != %02x", chk_calc, chk_recv);
        return DECODE_FAIL_MIC;
    }

    // Reflect bits in each byte
    reflect_bytes(manchester_buffer.bb[0], (manchester_buffer.bits_per_row[0] + 7) / 8);

    b = manchester_buffer.bb[0];

    // Extract channel (upper nibble + 1)
    uint8_t channel = ((b[5] >> 4) & 0x0F) + 1;

    // Extract device ID
    uint8_t device_id = b[6];

    // Extract temperature
    // BCD: Convert each nibble of byte 8 to decimal (tens+ones) and add decimal from byte 7
    float temperature = (((b[8] >> 4) & 0x0F) * 10 + (b[8] & 0x0F)) + ((b[7] >> 4) & 0x0F) * 0.1f;
    // Check sign byte (bit 4)
    if (b[9] & 0x08) {
        temperature = -temperature;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",        "", DATA_STRING, "Oria-WA150KM",
            "id",           "", DATA_INT,    device_id,
            "channel",      "", DATA_INT,    channel,
            "temperature",  "", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
            "mic",          "", DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "temperature",
        "mic",
        NULL,
};

r_device const oria_wa150km = {
        .name        = "Oria WA150KM freezer and fridge thermometer",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 490,
        .long_width  = 490,
        .gap_limit   = 1500,
        .reset_limit = 4000,
        .decode_fn   = &oria_wa150km_decode,
        .priority    = 10, // Reduce false positives with Oregon Scientific THGR810
        .fields      = output_fields,
};
