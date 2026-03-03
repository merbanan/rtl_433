/** @file
    Apator Metra E-RM 30 Water Meters.

    Copyright (C) 2025 Alex Carp (\@carpalex)
    Copyright (c) 2026 Bruno Octau (\@ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Apator Metra E-RM 30 Water Meters.

S.a issue #3012, for E-RM 30, #3452, for E-ITN 30

- Both E-RM 30 (Water Meter) and E-ITN 30 (Heat Cost Allocator) are using the same approach, same protocol.
- Only the message length differ between the 2 sensors.

Coding:
- Frames are transmitted with a preamble (0xaa 0xaa ...), followed by the 0x699a syncword.
- 2 levels of Data coding : It is first whitened using IBM Code, discovered into #3452, but the data payload are also encrypted.
- Each message is composed of one byte for the payload lengh, the encrypted payload and 2 byte for the CRC-16.
- Depends on the sensor, the payload length is : 19 byte for water meter and 17 byte for heat meter.
- CRC-16 must be checked after unwhitening and before decrypting the payload.
- The payload is encrypted using nibble substitution of 16 values.

E-RM 30 Message layout:

     Byte  0  1  2  3  4  5  6  7  8  9 10 11 12 13 15 16 17 18 19 20  21 22
     SSSS LL EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE  CC CC

- S  16b: syncword: 0x699a (16 bits)
- L   8b: payload length (seems to be always 19 = 0x13; does not include length and CRC)
- E 304b: encrypted payload (19 bytes), nibble substitution.
- C  16b: CRC-16 with poly=0x8005 and init=0xffff over data after IBM unwhitened but still coded (length field + encrypted payload)

Nibble substitution table:

| Coded | Decoded |
| --- | --- |
| 0 | 0 |
| 1 | 7 |
| 2 | F |
| 3 | 9 |
| 4 | E |
| 5 | D |
| 6 | 3 |
| 7 | 4 |
| 8 | 2 |
| 9 | 6 |
| A | C |
| B | B |
| C | 1 |
| D | 8 |
| E | A |
| F | 5 |

E-RM 30 Payload fields:

           0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
          II II II II VV VV VV VV ?? ?? ?? ?? ?? ?? ?? DD DD ?? ??

- I  32b: little-endian, id, visible on the radio module (not the one on the actual analog meter)
- V  25b: little-endian, volume in liters (or scale 1000 in m3), VOL = (little-endian of the 32b & 0x0fffffff) >> 3
- ?  56b: unknown
- D  16b: little-endian, date, bit distribution : Year (offset 2000) {7} Month {4} Day {5}
- ?  16b: unknown

According to the technical manual, the radio module also transmits other fields,
like reverse flow volume, date of magnetic tampering, date of mechanical tampering
etc., but they were not (yet) identified

*/

#include "decoder.h"

#define MAX_LEN 22     // 1 Byte LEN + 19 Byte MSG + 2 Byte CRC

#define CRC_LEN 2
#define LEN_LEN 1

#define ID_STR_LEN 9
#define VOL_STR_LEN 9
#define DATE_STR_LEN 10
#define BIT_LEN_STR_LEN 6

static int apator_metra_erm30_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {
                                 0xaa, 0xaa,  // preamble
                                 0x69, 0x9a   // sync word
    };

    uint8_t const ibm_whiten_key[22] = {0xff, 0xe1, 0x1d, 0x9a, 0xed, 0x85, 0x33, 0x24, 0xea, 0x7a, 0xd2, 0x39, 0x70, 0x97, 0x57, 0x0a, 0x54, 0x7d, 0x2d, 0xd8, 0x6d, 0x0d};

    if (bitbuffer->num_rows != 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_ABORT_EARLY;
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0, preamble, 8 * sizeof(preamble));

    if (start_pos == bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 1, __func__, "Preamble Sync word not found");
        return DECODE_ABORT_EARLY; // no preamble and / or sync word detected
    }

    uint8_t len;
    bitbuffer_extract_bytes(bitbuffer, 0, start_pos + 8 * sizeof(preamble), &len, 8);

    decoder_logf(decoder, 1, __func__, "MSG LEN: %d", len);

    len ^= 0xff;
    if (len != 0x13) {
        decoder_logf(decoder, 1, __func__, "MSG LEN does not match 19: %d", len);
        return DECODE_ABORT_EARLY; // unknown model
    }

    uint8_t frame[MAX_LEN] = {0}; // uint8_t max bytes + 2 bytes crc + 1 byte length
    // get frame (length field and CRC16 non included in len)
    bitbuffer_extract_bytes(bitbuffer, 0, start_pos + 8 * sizeof(preamble), frame, 8 * (MAX_LEN));

    // Unwhiten the data coded with IBM Whitening Algorithm LFSR, simple XOR is enough to decode.
    for (int i = 0; i < len + CRC_LEN + LEN_LEN; i++) {
        frame[i] ^= ibm_whiten_key[i];
    }

    decoder_log_bitrow(decoder, 1, __func__, frame, 8 * (MAX_LEN), "Unwhitened");

    uint16_t frame_crc = frame[len + 1] << 8 | frame[len + 2];
    uint16_t computed_crc = crc16(frame, len + LEN_LEN, 0x8005, 0xffff);
    if ( frame_crc != computed_crc) {
        decoder_logf(decoder, 1, __func__, "CRC 16 does not match, current %04x, expected %04x", frame_crc, computed_crc);
        return DECODE_FAIL_MIC;
    }

    //decoder_log(decoder, 1, __func__, "CRC 16 OK");

    uint8_t p[MAX_LEN] = {0};

    //decrypt the message, nibble substitution
    uint8_t const nibble_map[16] = {0x0, 0x7, 0xf, 0x9, 0xe, 0xd, 0x3, 0x4, 0x2, 0x6, 0xc, 0xb, 0x1, 0x8, 0xa, 0x5};

    for (int i = 0; i < 2 * len ; i++) {
        uint8_t nibble_encr, nibble_decr;

        unsigned int bitshift = (i % 2) ? 0 : 4;

        nibble_encr = (frame[1 + (i / 2)] >> bitshift) & 0x0f;
        nibble_decr = nibble_map[nibble_encr];

        p[i / 2] |= nibble_decr << bitshift;
    }

    decoder_log_bitrow(decoder, 1, __func__, p, 8 * (len), "MSG Decoded");

    uint32_t id      = (p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]) ^ 0x30000000;

    uint32_t vol_raw = ((p[7] << 24 | p[6] << 16 | p[5] << 8 | p[4]) & 0x0fffffff) >> 3;
    float    volume  = vol_raw / 1000.0f;
    uint16_t date    = p[16] << 8 | p[15];
    uint8_t  day     = date & 0x1f;
    uint8_t  month   = (date >> 5) & 0x0f;
    uint8_t  year    = (date >> 9) & 0x7f;

    char date_str[DATE_STR_LEN + 1];
    sprintf(date_str, "%04d-%02d-%02d", 2000 + year, month, day);

    /* clang-format off */
    data_t *data = data_make(
        "model",           "",                         DATA_STRING, "ApatorMetra-ERM30",
        "id",              "ID",                       DATA_FORMAT, "%09d",    DATA_INT,    id,
        "len",             "Frame length",             DATA_INT,    len,
        "volume_m3",       "Volume",                   DATA_FORMAT, "%.3f m3", DATA_DOUBLE, volume,
        "date",            "Date",                     DATA_STRING, date_str,
        "mic",             "Integrity",                DATA_STRING, "CRC",
        NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
    "model",
    "id",
    "len",
    "volume_m3",
    "date",
    "mic",
    NULL,
};

r_device const apator_metra_erm30 = {
    .name        = "Apator Metra E-RM 30 water meter",
    .modulation  = FSK_PULSE_PCM,
    .short_width = 25,
    .long_width  = 25,
    .reset_limit = 5000,
    .decode_fn   = &apator_metra_erm30_decode,
    .fields      = output_fields,
};
