/** @file
    Orion Me Enc Water Meter.

    Copyright (C) 2025 Bruno OCTAU (\@ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Orion Me Enc Water Meter.

Manufacturer: Badger Meter Inc

Model / FCCID : GIF2014W-OSE

- Water meter endpoint
- Issue #2995 open by \@ddffnn, other key contributors \@zuckschwerdt , \@jjemelka, \@klyubin , \@shawntoffel, others in the issue.
- Message is encoded using IBM Whitening Algorithm
- Other models look compatible, to be confirmed.

Flex decoder:

    rtl_433 -X "n=orion_me_enc,m=FSK_PCM,s=10,l=10,r=1000,preamble=aaaaec62ec62" 2>&1 | grep codes

    codes: {185}eb e1 1d 9a ed 6d 4a 4d e8 71 93 3a 78 23 57 0a ae ce 2d d8 7d 3f 4e 0
    codes: {184}eb e1 1d 9a ed 5f 99 7c e8 71 92 31 42 62 14 0a b3 95 6e d8 7d 59 7e

Data layout:

    Byte Position   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
    Sample         eb e1 1d 9a ed 6d 4a 4d e8 71 93 3a 78 23 57 0a ae ce 2d d8 7d 3f 4e 0
    unwhiten       14 00 00 00 00 e8 79 69 02 0b 41 03 08 b4 00 00 fa b3 00 00 10 32 f4 4
                   LL 11 11 11 11 SS SS SS SS xx |x xx RR RR RR RR DD DD DD DD 22 CC CC TT TT TT
                                                 |
                                              +--+---+
                                              | xxLx |
                                              +------+


- LL: {8} Message length except CRC, mostly 0x14 = 20 bytes, to be confirmed.
- II:{32} Fixed value, 0x00000000, could be reverse flow water counter ?
- SS: {32} Serial Number, little-endian value
- xx: Unknown, values look fixed and depends on the model, could be flags also, battery level too, to be guessed
- L:{1} Leak
- xx: other unknown values, flags, model, unit, battery low ? to be guessed.
- RR: {32} Reading value, scale 10 gallon, little-endian value
- DD: {32} Daily Reading Value, scale 10 gallon, little-endian value
- FF:{8} Fixed value, always 0x10
- CC:{16} CRC-16, poly 0x8005, init 0xFFFF, final XOR 0x0000, from previous 21 bytes.
- TT: Trailing bits

*/
static int orion_me_enc_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = { 0xaa, 0xaa, 0xec, 0x62, 0xec, 0x62};

    uint8_t b[23];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    int msg_len = bitbuffer->bits_per_row[0];

    if (msg_len > 290) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    int offset = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= msg_len) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    if ((msg_len - offset ) < 184 ) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 23 * 8);

    // Unwhiten the data coded with IBM Whitening Algorithm LFSR.
    ibm_whitening(b,23);

    decoder_log_bitrow(decoder, 2, __func__, b, 23 * 8, "Unwhiten MSG");

    if (crc16(b, 23, 0x8005, 0xffff)) {
        decoder_log(decoder, 1, __func__, "CRC 16 do not match");
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 2, __func__, b, 23 * 8, "MSG");

    // int msg_len   = b[0]; not used
    int id            = b[8] << 24 | b[7] << 16 | b[6] << 8 | b[5];
    int flags_1       = b[9] << 16 | b[10] << 8 | b[11];
    int leaking       = (b[10] & 0x20) >> 5;
    int reading_raw   = b[15] << 24 | b[14] << 16 | b[13] << 8 | b[12];
    int daily_raw     = b[19] << 24 | b[18] << 16 | b[17] << 8 | b[18];
    int flags_2       = b[20];
    float volume_gal  = reading_raw * 0.1f; // scale or decimal could defer
    float dly_vol_gal = daily_raw * 0.1f; // scale or decimal could defer

    /* clang-format off */
    data_t *data = data_make(
            "model",             "",                    DATA_STRING, "Orion-MEENC",
            "id",                "",                    DATA_INT,    id,
            "leaking",           "Leaking",             DATA_INT,    leaking,
            "volume_gal",        "Volume-Gallon",       DATA_FORMAT, "%.1f gal",  DATA_DOUBLE, volume_gal,
            "daily_volume_gal",  "Daily Volume-Gallon", DATA_FORMAT, "%.1f gal",  DATA_DOUBLE, dly_vol_gal,
            "flags_1",           "Flags-1",             DATA_FORMAT, "%06x", DATA_INT, flags_1,
            "flags_2",           "Flags-2",             DATA_FORMAT, "%02x", DATA_INT, flags_2,
            "mic",               "Integrity",           DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "volume_gal",
        "daily_volume_gal",
        "leaking",
        "flags_1",
        "flags_2",
        "mic",
        NULL,
};

r_device const orion_me_enc = {
        .name        = "Orion ME ENC from Badger Meter, GIF2014W-OSE, water meter, 100kbps (-f 916.7M -s 1600k)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 10,
        .long_width  = 10,
        .reset_limit = 1000,
        .decode_fn   = &orion_me_enc_decode,
        .fields      = output_fields,
};
