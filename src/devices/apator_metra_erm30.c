/** @file
    Apator Metra E-RM 30 Electronic Radio Module for Residential Water Meters.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/** @fn int apator_metra_erm30_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Apator Metra E-RM 30 Electronic Radio Module for Residential Water Meters.

All messages appear to have the same length and are transmitted with a preamble
(0x55 0x55), followed by the 0x9665 syncword. The bitstream is inverted. The
length and CRC-16 are transmitted in clear text, while the payload is encrypted
with an algoritm that seems to be custom, based on 4x4 S-boxes.

Message layout:

           0  1 2 3 ...........................0x13 0x15
     SSSS LL EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE CCCC

- S  16b: syncword: 0x9665 (16 bits)
- L   8b: payload length (seems to be always 19 = 0x13; does not include length and CRC)
- E 304b: encrypted payload (19 bytes)
- C  16b: CRC-16 with poly=0x8005 and init=0xfcad over data (length field and
          encrypted payload) after sync and bitstream invert


Payload fields:

           0 1 2 3  4 5 6 7 .............. 0x10 ....
          IIIIIIII VVVVVVVV ?????????????? DDDD ????

- I  32b: id, visible on the radio module (not the one on the actual analog meter)
- V  32b: volume in liters
- ?  56b: unknown
- D  16b: date, bitpacked before encryption
- ?  16b: unknown

According to the technical manual, the radio module also transmits other fields,
like reverse flow volume, date of magnetic tampering, date of mechanical tampering
etc., but they were not (yet) identified

*/

#include "decoder.h"

#define MAX_LEN 256
#define KEY_SCHEDULE_LEN 38

#define CRC_LEN 2
#define LEN_LEN 1

#define ID_STR_LEN 9
#define VOL_STR_LEN 9
#define DATE_STR_LEN 10
#define BIT_LEN_STR_LEN 6

static void decrypt_payload(uint8_t plen, uint8_t *payload_encr, uint8_t *payload_decr, uint8_t *decr_mask);
static void extract_id(uint8_t *p, uint8_t *m, char *id_str);
static void extract_volume(uint8_t *p, uint8_t *m, char *volume_str);
static void extract_date(uint8_t *p, uint8_t *m, char *date_str);

static int apator_metra_erm30_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {
        /* 0x55, ..., 0x55, */ 0x55, 0x55,  // preamble
        0x96, 0x65                          // sync word
    };

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    bitbuffer_invert(bitbuffer);

    int row = 0;
    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, 8 * sizeof(preamble));

    if (start_pos == bitbuffer->bits_per_row[row]) {
        return DECODE_ABORT_EARLY; // no preamble and / or sync word detected
    }

    uint8_t len;
    bitbuffer_extract_bytes(bitbuffer, row, start_pos + 8 * sizeof(preamble), &len, 8);

    uint8_t frame[MAX_LEN + CRC_LEN + LEN_LEN] = {0}; // uint8_t max bytes + 2 bytes crc + 1 byte length
    // get frame (length field and CRC16 non included in len)
    bitbuffer_extract_bytes(bitbuffer, row, start_pos + 8 * sizeof(preamble), frame, 8 * (len + CRC_LEN + LEN_LEN));

    uint16_t frame_crc = frame[len + 1] << 8 | frame[len + 2];
    uint16_t computed_crc = crc16(frame, len + LEN_LEN, 0x8005, 0xfcad);
    if (frame_crc != computed_crc) {
        return DECODE_FAIL_MIC;
    }

    uint8_t *payload_encr = frame + LEN_LEN;
    uint8_t payload_decr[MAX_LEN] = {0};
    uint8_t decr_mask[MAX_LEN] = {0};

    decrypt_payload(len, payload_encr, payload_decr, decr_mask);

    char id[ID_STR_LEN + 1];
    extract_id(payload_decr, decr_mask, id);

    char volume[VOL_STR_LEN + 1];
    extract_volume(payload_decr, decr_mask, volume);

    char date[DATE_STR_LEN + 1];
    extract_date(payload_decr, decr_mask, date);

    /* clang-format off */
    data_t *data = data_make(
        "model",        "",                         DATA_STRING,    "ApatorMetra-ERM30",
        "id",           "ID",                       DATA_STRING,    id,
        "len",          "Frame length",             DATA_INT,       len,
        "volume_m3",    "Volume",                   DATA_STRING,    volume,
        "date",         "Date",                     DATA_STRING,    date,
        "mic",          "Integrity",                DATA_STRING,    "CRC",
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
    .name        = "Apator Metra E-RM 30",
    .modulation  = FSK_PULSE_PCM,
    .short_width = 25,
    .long_width  = 25,
    .reset_limit = 5000,
    .decode_fn   = &apator_metra_erm30_decode,
    .fields      = output_fields,
};



/*
Decrypts an encrypted payload according to the S-boxes and key-schedule.
The encrypted payload is read from the payload_encr buffer and the decrypted payload is written in payload_decr.

There is also the decr_mask buffer, which acts like a "decryption bitmap": if a nibble was decrypted, the mask
at the correspoding offset is 0x0, otherwise is 0xf. It's used when converting the (partially) decrypted values
to strings.

It has been observed that there are 16 possible S-boxes. They are derived by writing the first one as a 4x4
matrix and permutting the rows and columns. The "name" of the S-box related to where the "0" is in the
corresponding matrix (e.g. sbox_2_3 has the 0 in row 2, column 3.

It couldn't be determined where all S-boxes are used, so the ones with unknown usage are listed here as commented.

The key_schedule array actually maps the offset of the encrypted nibble to the sbox that must be used for
decryption. If we didn't figure out which sbox to use, it has NULL for that offset.
*/
static void decrypt_payload(uint8_t plen, uint8_t *payload_encr, uint8_t *payload_decr, uint8_t *decr_mask)
{
    // uint8_t const sbox_0_0[16] = {0x0, 0x7, 0xf, 0x9, 0xe, 0xd, 0x3, 0x4, 0x2, 0x6, 0xc, 0xb, 0x1, 0x8, 0xa, 0x5};
    uint8_t const sbox_0_1[16] = {0x7, 0x0, 0x9, 0xf, 0xd, 0xe, 0x4, 0x3, 0x6, 0x2, 0xb, 0xc, 0x8, 0x1, 0x5, 0xa};
    uint8_t const sbox_0_2[16] = {0xf, 0x9, 0x0, 0x7, 0x3, 0x4, 0xe, 0xd, 0xc, 0xb, 0x2, 0x6, 0xa, 0x5, 0x1, 0x8};
    // uint8_t const sbox_0_3[16] = {0x9, 0xf, 0x7, 0x0, 0x4, 0x3, 0xd, 0xe, 0xb, 0xc, 0x6, 0x2, 0x5, 0xa, 0x8, 0x1};
    // uint8_t const sbox_1_0[16] = {0xe, 0xd, 0x3, 0x4, 0x0, 0x7, 0xf, 0x9, 0x1, 0x8, 0xa, 0x5, 0x2, 0x6, 0xc, 0xb};
    uint8_t const sbox_1_1[16] = {0xd, 0xe, 0x4, 0x3, 0x7, 0x0, 0x9, 0xf, 0x8, 0x1, 0x5, 0xa, 0x6, 0x2, 0xb, 0xc};
    uint8_t const sbox_1_2[16] = {0x3, 0x4, 0xe, 0xd, 0xf, 0x9, 0x0, 0x7, 0xa, 0x5, 0x1, 0x8, 0xc, 0xb, 0x2, 0x6};
    uint8_t const sbox_1_3[16] = {0x4, 0x3, 0xd, 0xe, 0x9, 0xf, 0x7, 0x0, 0x5, 0xa, 0x8, 0x1, 0xb, 0xc, 0x6, 0x2};
    uint8_t const sbox_2_0[16] = {0x2, 0x6, 0xc, 0xb, 0x1, 0x8, 0xa, 0x5, 0x0, 0x7, 0xf, 0x9, 0xe, 0xd, 0x3, 0x4};
    // uint8_t const sbox_2_1[16] = {0x6, 0x2, 0xb, 0xc, 0x8, 0x1, 0x5, 0xa, 0x7, 0x0, 0x9, 0xf, 0xd, 0xe, 0x4, 0x3};
    uint8_t const sbox_2_2[16] = {0xc, 0xb, 0x2, 0x6, 0xa, 0x5, 0x1, 0x8, 0xf, 0x9, 0x0, 0x7, 0x3, 0x4, 0xe, 0xd};
    uint8_t const sbox_2_3[16] = {0xb, 0xc, 0x6, 0x2, 0x5, 0xa, 0x8, 0x1, 0x9, 0xf, 0x7, 0x0, 0x4, 0x3, 0xd, 0xe};
    uint8_t const sbox_3_0[16] = {0x1, 0x8, 0xa, 0x5, 0x2, 0x6, 0xc, 0xb, 0xe, 0xd, 0x3, 0x4, 0x0, 0x7, 0xf, 0x9};
    uint8_t const sbox_3_1[16] = {0x8, 0x1, 0x5, 0xa, 0x6, 0x2, 0xb, 0xc, 0xd, 0xe, 0x4, 0x3, 0x7, 0x0, 0x9, 0xf};
    uint8_t const sbox_3_2[16] = {0xa, 0x5, 0x1, 0x8, 0xc, 0xb, 0x2, 0x6, 0x3, 0x4, 0xe, 0xd, 0xf, 0x9, 0x0, 0x7};
    // uint8_t const sbox_3_3[16] = {0x5, 0xa, 0x8, 0x1, 0xb, 0xc, 0x6, 0x2, 0x4, 0x3, 0xd, 0xe, 0x9, 0xf, 0x7, 0x0};

    uint8_t const *const key_schedule[KEY_SCHEDULE_LEN] = {
        sbox_0_1, sbox_3_2, sbox_3_2, sbox_0_2, sbox_1_2, sbox_1_1, sbox_1_1, sbox_0_2,
        sbox_1_3, sbox_2_2, sbox_3_0, sbox_3_0, sbox_3_1, sbox_2_3, NULL,     sbox_1_1,
        NULL,     NULL,     NULL,     NULL,     NULL,     NULL,     NULL,     NULL,
        NULL,     NULL,     NULL,     NULL,     NULL,     NULL,     sbox_2_2, sbox_2_3,
        sbox_2_0, sbox_0_2, NULL,     NULL,     NULL,     NULL,
    };

    for (int i = 0; i < 2 * plen; i++) {
        uint8_t nibble_encr, nibble_decr, nibble_mask;

        unsigned int bitshift = (i % 2) ? 0 : 4;

        if (i < KEY_SCHEDULE_LEN && key_schedule[i] != NULL) {
            nibble_encr = (payload_encr[i / 2] >> bitshift) & 0x0f;
            nibble_decr = key_schedule[i][nibble_encr];
            nibble_mask = 0x0;
        } else {
            nibble_decr = 0x0;
            nibble_mask = 0xf;
        }

        payload_decr[i / 2] |= nibble_decr << bitshift;
        decr_mask[i / 2] |= nibble_mask << bitshift;
    }
}

/*
Converts the binary value of the ID field to a string that can be pretty-printed.
If the field was only partially decrypted, the string will contain question marks.
*/
static void extract_id(uint8_t *p, uint8_t *m, char *id_str)
{
    uint32_t id = p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0];
    uint32_t mask = m[3] << 24 | m[2] << 16 | m[1] << 8 | m[0];

    if (mask == 0) {
        sprintf(id_str, "%09d", id);
    } else {
        sprintf(id_str, "????????");
    }
}


/*
Converts the binary value of the Volume field to a string that can be pretty-printed.
If the field was only partially decrypted, the string will contain question marks.
*/
static void extract_volume(uint8_t *p, uint8_t *m, char *volume_str)
{
    uint32_t volume = ((p[7] << 24 | p[6] << 16 | p[5] << 8 | p[4]) & 0x0fffffff) >> 3;
    uint32_t mask = ((m[7] << 24 | m[6] << 24 | m[5] << 8 | m[4]) & 0x0fffffff) >> 3;

    if (mask == 0) {
        sprintf(volume_str, "%.3f", volume / 1000.0);
    } else {
        sprintf(volume_str, "?????.???");
    }
}

/*
Converts the binary value of the Date field to a string that can be pretty-printed.
If the field was only partially decrypted, the string will contain question marks.
*/
static void extract_date(uint8_t *p, uint8_t *m, char *date_str)
{
    uint16_t date = p[16] << 8 | p[15];
    uint16_t mask = m[16] << 8 | m[15];

    if (mask == 0) {
        uint8_t day = date & 0x1f;
        uint8_t month = (date >> 5) & 0x0f;
        uint8_t year = (date >> 9) & 0x7f;
        sprintf(date_str, "%04d-%02d-%02d", 2000 + year, month, day);
    } else {
        sprintf(date_str, "%s-%s-%s", "????", "??", "??");
    }
}
