/** @file
    Badger ORION water meter support.

    Copyright (C) 2022 Nicko van Someren

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn static int badger_orion_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Badger ORION water meter.

S.a. https://fccid.io/GIF2006B

For the single-frequency models the center frequency is 916.45MHz. The bit rate is
100KHz, so the sample rate should be at least 1.2MHz; using 1.6MHz may work better
when the signal is weak or noisy.

The low-level encoding is much the same as M-Bus mode T, but the payload differs.

The specification sheet states that "The endpoint broadcasts its unique endpoint
serial number, current meter reading and applicable status indicators" and also that
status reports include "Premise Leak Detection", "Cut-Wire Indication", "Reverse Flow
Indication", "No Usage Indication" and "Encoder Error", but the specific flag values
are not known.

The data is preceded by several sync bytes of 01010101, followed by the ten bit
preamble of 0000 1111 01. This is followed by 10 bytes encoded using a 4:6 NRZ
encoding. This code treats 6 bits of the sync sequence as part of a 16 bit preamble.

Once the data has been decoded with the NRZ 6:4 decoding, it has the following format:
- Device ID: 3 bytes, little-endian. Typically utility provider's number, mod 2^24 or mod 10^7.
- Device flags: 1 byte. Fields not known
- Meter reading: 3 bytes, little-endian. Value in gallons for meters with 1-gallon resolution.
- Status flags: 1 byte. Fields not known
- CRC: 2 bytes, crc16, polynomial 0x3D65

*/

// Mapping from 6 bits to 4 bits. "3of6" coding used for Mode T
static uint8_t badger_decode_3of6(uint8_t byte)
{
    uint8_t out = 0xFF; // Error
    switch(byte) {
    case 22:    out = 0x0;  break;  // 0x16
    case 13:    out = 0x1;  break;  // 0x0D
    case 14:    out = 0x2;  break;  // 0x0E
    case 11:    out = 0x3;  break;  // 0x0B
    case 28:    out = 0x4;  break;  // 0x1C
    case 25:    out = 0x5;  break;  // 0x19
    case 26:    out = 0x6;  break;  // 0x1A
    case 19:    out = 0x7;  break;  // 0x13
    case 44:    out = 0x8;  break;  // 0x2C
    case 37:    out = 0x9;  break;  // 0x25
    case 38:    out = 0xA;  break;  // 0x26
    case 35:    out = 0xB;  break;  // 0x23
    case 52:    out = 0xC;  break;  // 0x34
    case 49:    out = 0xD;  break;  // 0x31
    case 50:    out = 0xE;  break;  // 0x32
    case 41:    out = 0xF;  break;  // 0x29
    default:    break;  // Error
    }
    return out;
}

// Decode the DC-free 4:6 encoding
static int badger_decode_3of6_buffer(uint8_t const *bits, unsigned bit_offset, uint8_t *output)
{
    for (unsigned n=0; n<10; ++n) {
        uint8_t nibble_h = badger_decode_3of6(bitrow_get_byte(bits, n*12+bit_offset) >> 2);
        uint8_t nibble_l = badger_decode_3of6(bitrow_get_byte(bits, n*12+bit_offset+6) >> 2);
        if ((nibble_h | nibble_l) > 15) {
            return 1;
        }
        output[n] = (nibble_h << 4) | nibble_l;
    }
    return 0;
}

static int badger_orion_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    static uint8_t const preamble_pattern[] = {0x54, 0x3D};

    uint8_t data_in[10] = {0}; // Data from Physical layer decoded to bytes
    data_t *data;

    // Validate package length
    // The minimum preamble is 16 bits and the payload is 10 4:6 encoded bytes.
    // There is often a long preamble and a 64 or more bits of tail, so the maximum likely length is longer
    if (bitbuffer->bits_per_row[0] < (16 + 12 * 10) || bitbuffer->bits_per_row[0] > (128 + 16 + 12 * 10 + 96)) {
        return DECODE_ABORT_LENGTH;
    }

    // Find the preamble
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);
    if (bit_offset + 12 * 10 >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }

    decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "Preamble found at: %u", bit_offset);
    bit_offset += sizeof(preamble_pattern) * 8; // skip preamble

    if (badger_decode_3of6_buffer(bitbuffer->bb[0], bit_offset, data_in) < 0) {
        return DECODE_FAIL_MIC;
    }

    uint16_t crc_read = (((uint16_t)data_in[8] << 8) | data_in[9]);
    uint16_t crc_calc = ~crc16(data_in, 8, 0x3D65, 0);
    if (crc_calc != crc_read) {
        decoder_logf(decoder, 1, __func__,
                "Badger ORION: CRC error: Calculated 0x%0X, Read 0x%0X",
                (unsigned)crc_calc, (unsigned) crc_read);
        return DECODE_FAIL_MIC;
    }

    uint32_t device_id = data_in[0] | (data_in[1] << 8) | (data_in[2] << 16);
    uint8_t flags_1 = data_in[3];
    uint32_t volume = data_in[4] | (data_in[5] << 8) | (data_in[6] << 16);
    uint8_t flags_2 = data_in[7];

    /* clang-format off */
    data = data_make(
            "model",      "",          DATA_STRING, "Badger-ORION",
            "id",         "ID",        DATA_INT,    device_id,
            "flags_1",    "Flags-1",   DATA_INT,    flags_1,
            "volume_gal", "Volume",    DATA_INT,    volume,
            "flags_2",    "Flags-2",   DATA_INT,    flags_2,
            "mic",        "Integrity", DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 0;
}

// Note: At this time the exact meaning of the flags is not known.
static char const *const badger_output_fields[] = {
        "model",
        "id",
        "flags_1",
        "volume_gal",
        "flags_2",
        "mic",
        NULL,
};

// Badger ORION water meter,
// Frequency 916.45 MHz, Bitrate 100 kbps, Modulation NRZ FSK
r_device const badger_orion = {
        .name        = "Badger ORION water meter, 100kbps (-f 916.45M -s 1200k)", // Minimum samplerate = 1.2 MHz (12 samples of 100kb/s)
        .modulation  = FSK_PULSE_PCM,
        .short_width = 10,   // Bit rate: 100 kb/s
        .long_width  = 10,   // NRZ encoding (bit width = pulse width)
        .reset_limit = 1000, //
        .decode_fn   = &badger_orion_decode,
        .fields      = badger_output_fields,
};
