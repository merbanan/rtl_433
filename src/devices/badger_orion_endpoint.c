/** @file
    Orion Water Endpoint Meter.

    Copyright (C) 2025 Bruno OCTAU (\@ProfBoc75), \@klyubin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define PREAMBLE_BYTELEN   6
#define DATA_BYTELEN      23

#define PREAMBLE_BITLEN   48  //  6*8
#define DATA_BITLEN      184  // 23*8

#define MSG_MIN_BITLEN   232  // PREAMBLE+DATA
#define MSG_MAX_BITLEN   290  // PREAMBLE+DATA+TRAILING_BITS

/**
Orion Water Endpoint Meter.

- Issue #2995 open by \@ddffnn, other key contributors \@zuckschwerdt , \@jjemelka, \@klyubin , \@shawntoffel, ... others in the issue.

Manufacturer: Badger Meter Inc

FCCID : GIF2014W-OSE

- Orion Cellular Endpoint, water meter, several models based on the Serial Number, According to https://badgermeter.widen.net/content/vodetxkyxh/original?download=false&x.app=api:

Serial number ranges:
-  30 000 000 …  59 999 999: ME or SE
-  60 000 000 …  69 999 999: Mobile M
-  70 000 000 …  89 999 999: Classic (CE)
- 110 000 000 … 119 999 999: LTE
- 120 000 000 … 129 999 999: LTE-M or LTE-MS
- 130 000 000 … 139 999 999: C or CS
- 140 000 000 … 148 999 999: HLA
- 149 000 000 … 149 999 999: HLC
- 150 000 000 … 159 999 999: HLB
- 160 000 000 … 169 999 999: HLD
- 170 000 000 … 179 999 999: HLFX
- 180 000 000 … 189 999 999: HLG

All models above may not be compatible with this decoder as few of them are using mobile frequency, like LTE models.

Frequency Hopping Spread Spectrum Intentional Radiators Operating within the 902-928MHz band, source : https://fcc.report/FCC-ID/GIF2014W-OSE/2499315

2 Hopping options, Fixed Mode or Mobile Mode:
- Fixed Mode, 400 Khz between channels, total of 50 channels from 904.56 Mhz to 924.56 Mhz
- Mobile Mode, 400.55 Khz between channels, total of 48 channels from 904.45 Mhz to 923.675 Mhz

Frequency channel changed every 150 secondes (#2995)

- Message is encoded using IBM Whitening Algorithm.

Flex decoder:

    rtl_433 -X "n=orion_endpoint,m=FSK_PCM,s=10,l=10,r=1000,preamble=aaaaec62ec62" 2>&1 | grep codes

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
static int orion_endpoint_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[PREAMBLE_BYTELEN] = {0xaa, 0xaa, 0xec, 0x62, 0xec, 0x62};

    // ibm_whiten() function added to bit_util but requires CPU cycles, as we just need XOR with fixed value this will take less resources.
    uint8_t const ibm_whiten_key[DATA_BYTELEN] = {0xff, 0xe1, 0x1d, 0x9a, 0xed, 0x85, 0x33, 0x24, 0xea, 0x7a, 0xd2, 0x39, 0x70, 0x97, 0x57, 0x0a, 0x54, 0x7d, 0x2d, 0xd8, 0x6d, 0x0d, 0xba};

    uint8_t b[DATA_BYTELEN];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    int msg_len = bitbuffer->bits_per_row[0];

    if (msg_len < MSG_MIN_BITLEN || msg_len > MSG_MAX_BITLEN) {
        decoder_logf(decoder, 1, __func__, "Message length error: must be between 232 and 290 bits, found %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    int offset = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, PREAMBLE_BITLEN);

    if (offset >= msg_len) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    offset += PREAMBLE_BITLEN;

    if ((msg_len - offset ) < DATA_BITLEN ) {
        decoder_logf(decoder, 1, __func__, "Expected %d bits, Packet too short: %d bits", DATA_BITLEN, msg_len - offset);
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, DATA_BITLEN);

    // Unwhiten the data coded with IBM Whitening Algorithm LFSR.
    // ibm_whitening(b,23); // replace by XOR from ibm_whiten_key to reduce CPU cycles and resources.
    for (int i = 0; i < DATA_BYTELEN; i++) {
        b[i] ^= ibm_whiten_key[i];
    }

    decoder_log_bitrow(decoder, 2, __func__, b, DATA_BITLEN, "Unwhiten MSG");

    if (crc16(b, DATA_BYTELEN, 0x8005, 0xffff)) {
        decoder_log(decoder, 1, __func__, "CRC 16 do not match");
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 2, __func__, b, DATA_BITLEN, "Valid MSG");

    // uint8_t msg_len   = b[0]; not used
    uint32_t id            = b[8] << 24 | b[7] << 16 | b[6] << 8 | b[5];
    uint32_t flags_1       = b[9] << 16 | b[10] << 8 | b[11];
    int leaking            = (b[10] & 0x20) >> 5;
    uint32_t reading_raw   = b[15] << 24 | b[14] << 16 | b[13] << 8 | b[12];
    uint32_t daily_raw     = b[19] << 24 | b[18] << 16 | b[17] << 8 | b[16];
    uint8_t flags_2        = b[20];

    // Define Endpoint Model
    char const *endpoint_model = "Unknown Model";
    if (id >= 30000000 && id <= 59999999) {
        endpoint_model = "ME or SE";
    }
    else if (id >= 60000000 && id <= 69999999) {
        endpoint_model = "Mobile M";
    }
    else if (id >= 70000000 && id <= 89999999) {
        endpoint_model = "Classic (CE)";
    }
    else if (id >= 110000000 && id <= 119999999) {
        endpoint_model = "LTE";
    }
    else if (id >= 120000000 && id <= 129999999) {
        endpoint_model = "LTE-M or LTE-MS";
    }
    else if (id >= 130000000 && id <= 139999999) {
        endpoint_model = "C or CS";
    }
    else if (id >= 140000000 && id <= 148999999) {
        endpoint_model = "HLA";
    }
    else if (id >= 149000000 && id <= 149999999) {
        endpoint_model = "HLC";
    }
    else if (id >= 150000000 && id <= 159999999) {
        endpoint_model = "HLB";
    }
    else if (id >= 160000000 && id <= 169999999) {
        endpoint_model = "HLD";
    }
    else if (id >= 170000000 && id <= 179999999) {
        endpoint_model = "HLFX";
    }
    else if (id >= 180000000 && id <= 189999999) {
        endpoint_model = "HLG";
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",             "",                    DATA_STRING, "Orion-Endpoint",
            "id",                "",                    DATA_INT,    id,
            "endpoint_model",    "Endpoint Model",      DATA_STRING, endpoint_model,
            "leaking",           "Leaking",             DATA_INT,    leaking,
            "reading",           "Reading",             DATA_INT,    reading_raw,
            "daily_reading",     "Daily Reading",       DATA_COND,   daily_raw, DATA_INT, daily_raw,
            "flags_1",           "Flags-1",             DATA_FORMAT, "%06x",    DATA_INT, flags_1,
            "flags_2",           "Flags-2",             DATA_FORMAT, "%02x",    DATA_INT, flags_2,
            "mic",               "Integrity",           DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "endpoint_model",
        "leaking",
        "reading",
        "daily_reading",
        "flags_1",
        "flags_2",
        "mic",
        NULL,
};

r_device const orion_endpoint = {
        .name        = "Orion Endpoint from Badger Meter, GIF2014W-OSE, water meter, hopping from 904.4 Mhz to 924.6Mhz (-s 1600k)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 10,
        .long_width  = 10,
        .reset_limit = 1000,
        .decode_fn   = &orion_endpoint_decode,
        .fields      = output_fields,
};

r_device const orion_endpoint_2020 = {
        .name        = "Orion Endpoint from Badger Meter, GIF2020OCECNA, water meter, hopping from 904.4 Mhz to 924.6Mhz (-s 1600k)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 5,
        .long_width  = 5,
        .reset_limit = 1000,
        .decode_fn   = &orion_endpoint_decode,
        .fields      = output_fields,
};
