/** @file
    Apator Metra E-ITN 30 Heat cost allocator.

    Copyright (C) 2025 Alex Carp (\@carpalex)
    Copyright (c) 2026 Bruno Octau (\@ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Apator Metra E-ITN 30 Heat cost allocator.

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

E-ITN 30:

Flex decoder:

    rtl_433 -X "n=Apator_eitn30,m=FSK_PCM,s=25,l=25,r=5000, preamble=aaaa699a"

    Sample   ee c2 5e db 8e 00 3d 15 84 ca df 36 78 f9 30 c1 f7 bd c6 ec
    Sample   ee c2 5e db 8e 00 3d 15 84 ca df 34 78 f9 30 0a 82 bd f5 57
    Sample   ee c2 5e db 8e 00 3d 15 84 ca 5f 32 78 f9 30 c5 eb bd 89 cd
    Sample   ee c2 5e db 8e 00 3d 15 84 ca 5f f8 78 fc 30 85 ef bd 53 f5
    Sample   ee c2 5e db 8e 00 3d 15 84 ca 1f fc 78 fc 30 64 8c bd 91 bc

    UnWhiten 11 23 43 41 63 85 0e 31 6e b0 0d 0f 08 6e 67 cb a3 c0 eb 34
    UnWhiten 11 23 43 41 63 85 0e 31 6e b0 0d 0d 08 6e 67 00 d6 c0 d8 8f
    UnWhiten 11 23 43 41 63 85 0e 31 6e b0 8d 0b 08 6e 67 cf bf c0 a4 15
    UnWhiten 11 23 43 41 63 85 0e 31 6e b0 8d c1 08 6b 67 8f bb c0 7e 2d
    UnWhiten 11 23 43 41 63 85 0e 31 6e b0 cd c5 08 6b 67 6e d8 c0 bc 64


Data layout:

    Byte Position   0   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17  18 19
    unwhiten       11  23 43 41 63 85 0e 31 6e b0 0d 0f 08 6e 67 cb a3 c0  eb 34
                   LL  EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE  CC CC

- LL: {8} Message length except CRC, 0x11 = 17 bytes.
- EE: {136} Encrypted message, see substitution table
- CC:{16} CRC-16, poly 0x8005, init 0xFFFF, final XOR 0x0000, over data after IBM unwhitened but still coded.

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

Payload:

    Byte Position   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16
    unwhitened     23 43 41 63 85 0e 31 6e b0 0d 0f 08 6e 67 cb a3 c0
    decoded        F9 E9 E7 39 2D 0A 97 3A B0 08 05 02 3A 34 1B C9 10
                   II II II II PP PP ?? ?? ?? ?? VV VV MD YY ?? ?? ??

- II: {25} little endian, serial number of the sensor
- PP: {16} little endian, last year value
- VV: {16} little endian, current value
- MDYY {16} little endian, current date, distributed like that : YEAR offset 2000 {7} MONTH {4} DAY {5}
- ??: Unknown value

*/

#include "decoder.h"

#define MAX_LEN 20     // 1 Byte LEN + 17 Byte MSG + 2 Byte CRC

#define CRC_LEN 2
#define LEN_LEN 1

#define ID_STR_LEN 9
#define VOL_STR_LEN 9
#define DATE_STR_LEN 10
#define BIT_LEN_STR_LEN 6

static int apator_metra_eitn30_decode(r_device *decoder, bitbuffer_t *bitbuffer)
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
    if (len != 0x11) {
        decoder_logf(decoder, 1, __func__, "MSG LEN does not match 17: %d", len);
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

    uint32_t id      = (p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]) ^ 0x38000000;

    uint16_t current = p[11] << 8 | p[10];
    uint16_t last_yr = p[5]  << 8 | p[4];
    uint16_t date    = p[13] << 8 | p[12];
    uint8_t  day     = date & 0x1f;
    uint8_t  month   = (date >> 5) & 0x0f;
    uint8_t  year    = (date >> 9) & 0x7f;

    char date_str[DATE_STR_LEN + 1];
    sprintf(date_str, "%04d-%02d-%02d", 2000 + year, month, day);

    /* clang-format off */
    data_t *data = data_make(
        "model",           "",                         DATA_STRING, "ApatorMetra-EITN30",
        "id",              "ID",                       DATA_FORMAT, "%09d",  DATA_INT,    id,
        "len",             "Frame length",             DATA_INT,    len,
        "current_heating", "Current Heating",          DATA_INT,    current,
        "last_yr_heating", "Last Year Heating",        DATA_INT,    last_yr,
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
    "current_heating",
    "last_yr_heating",
    "date",
    "mic",
    NULL,
};

r_device const apator_metra_eitn30 = {
    .name        = "Apator Metra E-ITN 30 heat cost allocator",
    .modulation  = FSK_PULSE_PCM,
    .short_width = 25,
    .long_width  = 25,
    .reset_limit = 5000,
    .decode_fn   = &apator_metra_eitn30_decode,
    .fields      = output_fields,
};
