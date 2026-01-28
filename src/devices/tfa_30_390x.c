/** @file

    TFA Dostmann A5 30.3901.02 temperature sensor.
    TFA Dostmann A3 30.3902.02 temperature sensor with external sensor.
    TFA Dostmann A4 30.3905.02 temperature and humidity sensor with external temp sensor.
    TFA Dostmann A6 30.3906.02 temperature and humidity sensor.
    TFA Dostmann A0 30.3908.02 temperature and humidity sensor (big display).

    Copyright (c) 2026 Jacob Maxa <jack77@gmx.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/

/*  @fn int  tfa_30390X_decode(r_device *decoder, bitbuffer_t *bitbuffer)

    All sensors work on 868.025 Mhz with 250 kHz sample rate/bandwidth

    This device is part of the ID+ sensor system with tfa.me cloud ability.

    Depending on the sensors capability ist encoded in by name within the first ID byte (see above)
    The data frames depends on the device. Formated as follows:
    - LL - packet length
    - ID - unique 9 byte long address of the sensor, printed on its back, marks the senders capability
    - S  - status nibble (0xMXBX)
        - M - manual forced transmission by button press on device ( = 1)
        - B - battery OK flag; 0 = batt OK, 1 = batt low
    - CNT - Up-counter to detect missing packets (2 byte, little-endian)
    - T_INT - internal device temperature  (2 byte, little-endian), must be divided by 10
    - HUMID - humidity (2 byte, little-endian),  must be divided by 10
    - T_EXT - see T_INT
    - OFFSE - Offset in seconds from previous frame
    - CRC32 - checksum, see below

    The sensors send the current value followed by the last two values in case a packet was lost.
    In combination with the counter value lost packets can be recovered.

    -1 0 1 2 3 4__ 5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34      <<< byte position in 'b' array

    A0 and A6: internal temperature + humidity
    |   Header          | Current frame   | Current frame-1 | Current frame-2 |
    LL ID_______ S COUNT T_INT HUMID OFFSE T_INT HUMID OFFSE T_INT HUMID OFFSE CRC32______
    1e a051fc6c2 2 0c 00 d9 00 22 01 05 00 d9 00 22 01 19 00 d7 00 2c 01 0d 00 cb 27 fa 60
    -1 0 1 2 3 4__ 5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28      <<< byte position in 'b' array + start_idx

    A3: internal + external temperature (normal range -40 - )
    |   Header          | Current Frame   | Current Frame -1| Current Frame -2|
    LL ID        S CNT__ T_INT T_EXT OFFSE T_INT T_EXT OFFSE T_INT T_EXT OFFSE CRC32______
    1e a3b02f727 2 34 19 e8 00 f4 00 3f 00 e2 00 f0 00 28 00 e0 00 e6 00 b8 00 b5 ab cc 2c
    -1 0 1 2 3 4__ 5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28       <<< byte position in 'b' array + start_idx

    A4: internal + external temperature (extended range) + humidity
    |   Header          | Current Frame         | Current Frame -1      | Current Frame -2      |
    LL ID        S CNT__ T_INT HUMID T_EXT OFFSE T_INT HUMID T_EXT OFFSE T_INT HUMID T_EXT OFFSE CRC32______
    24 a4903b641 0 5e 5f cf 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 6b f5 48 3c
    24 a4903b641 0 5f 5f ce 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 db 36 70 5a
    24 a4903b641 0 60 5f cf 00 68 01 c6 00 3c 00 ce 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 3e 7f 50 dd
    -1 0 1 2 3 4__ 5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34      <<< byte position in 'b' array + start_idx

    A5: internal temperature
        |   Header      | Cur Frame |  Frame -1 | Frame -2 |
    LL ID_______ S CNT__ T_INT OFSET T_INT OFSET T_INT OFSET CRC32______
    18 a529d7394 2 02 00 e3 00 08 00 e3 00 37 00 d9 00 02 00 85 7b 7d 7b
    -1 0 1 2 3 4__ 5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22       <<< byte position in 'b' array + start_idx

    A6: internal temperature + humidity
    |   Header          | Current frame   | Current frame-1 | Current frame-2 |
    LL ID_______ S COUNT T_INT HUMID OFFSE T_INT HUMID OFFSE T_INT HUMID OFFSE CRC32______
    1e a051fc6c2 2 0c 00 d9 00 22 01 05 00 d9 00 22 01 19 00 d7 00 2c 01 0d 00 cb 27 fa 60
    -1 0 1 2 3 4__ 5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28      <<< byte position in 'b' array + start_idx
*/

#include "decoder.h"
#include "logger.h"

#define TFA_30390X_MESSAGE_MIN_BITLEN   316
#define TFA_30390X_MESSAGE_MAX_BITLEN   320

#define TFA_30390X_CRC32_POLY_REFLECTED 0xEDB88320u  /* reflected default CRC32 0x04C11DB7 */
#define TFA_30390X_CRC32_INIT           0xFFFFFFFFu
#define TFA_30390X_CRC32_XOROUT         0xFFFFFFFFu

uint32_t crc32_reveng(uint8_t *data, size_t length);
/* *
 * CRC32 checksum calculation,
 * CRC32 nach RevEng:
 * width=32
 * poly=0x04C11DB7
 * init=0xFFFFFFFF
 * refin=true
 * refout=true
 * xorout=0xFFFFFFFF
 *
 * */


uint32_t crc32_reveng(uint8_t *data, size_t length)
{
    uint32_t crc = TFA_30390X_CRC32_INIT;

    while (length--) {
        crc ^= *data++;

        for (int i = 0; i < 8; i++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ TFA_30390X_CRC32_POLY_REFLECTED;
            } else {
                crc >>= 1;
            }
        }
    }

    crc ^= TFA_30390X_CRC32_XOROUT;
    return crc;
}
/*
    Decode value from data frame position
*/
static float decode_value(uint8_t* b, size_t start_idx, size_t offs_hi, uint8_t extended_range) {
    uint16_t raw = ((uint16_t)b[start_idx + offs_hi] << 8) | (uint16_t)b[start_idx + offs_hi - 1];
    int16_t val = 0;
    if(extended_range) {
        val = (int16_t)(raw << 4) >> 4; // 12-Bit sign-extended
    } else {
        val = (int16_t)(raw << 5) >> 5; // 13-Bit sign-extended
    }
    return (float)val / 10.0f;
}

static int tfa_30390X_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0x4b, 0x2d, 0xd4, 0x2b};  // preamble pattern for both sensors
    data_t *data    = {0};
    int i           = 0; // counter variable
    int start_idx   = 5; // offset for b array skipping the preamble
    int16_t tmp     = 0;

    uint16_t bitpos = 0;

    uint8_t b[TFA_30390X_MESSAGE_MIN_BITLEN] = {0};

    if(bitbuffer->bits_per_row[0] < TFA_30390X_MESSAGE_MIN_BITLEN) {
        print_logf(LOG_DEBUG, __func__, "package too short: %d bits received", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH;
    }

    bitpos = bitbuffer_search(bitbuffer, 0, bitpos, (const uint8_t *)&preamble_pattern, 32);

    if (bitpos >= TFA_30390X_MESSAGE_MIN_BITLEN) {
        print_logf(LOG_DEBUG, __func__, "message too long, skipping...");
        return DECODE_ABORT_LENGTH;
    }
    // read dataframe from recived bitbuffer
    bitbuffer_extract_bytes(bitbuffer, 0, bitpos, b, TFA_30390X_MESSAGE_MAX_BITLEN);

    uint32_t crc32_calculated = 0;
    uint32_t crc32_frame      = 0;

    uint8_t len = b[start_idx-1];

    crc32_calculated = crc32_reveng((uint8_t *)&b[start_idx-1], len-4);
    crc32_frame = ((uint32_t)b[start_idx + len - 2] << 24) | ((uint32_t)b[start_idx + len - 3] << 16) | ((uint32_t)b[start_idx + len - 4] << 8) | b[start_idx + len - 5];

    if (crc32_calculated != crc32_frame) {
        print_logf(LOG_DEBUG, __func__, "TFA 30.390X.02 CRC32 failed");
        return DECODE_FAIL_MIC;
    }
    // check for plausibility, if ID is all zero, receive was invalid
    for(i = 0; i < 5 ; i++){
        tmp += b[start_idx + i];
    }
    if(tmp == 0x0) {
        // invalid ID, abort
        print_logf(LOG_DEBUG, __func__, "TFA 30.390X.02 Invalid ID");
        return DECODE_FAIL_SANITY;
    }

    char id_str[30] = {0};
    // built human readable ID string
    sprintf(id_str, "%02X%02X%02X%02X%02X", b[start_idx + 0], b[start_idx + 1], b[start_idx + 2], b[start_idx + 3], b[start_idx + 4]);
    // remove last character as it is the status byte
    id_str[9] = 0;
    char model_name [21] = "TFA-30.390X.02 ID-A";
    model_name[19] = id_str[1];
    model_name[20] = 0; // null terminated string

    // status nibble : bit 3 LOW BATTERY, bit 1 MANUAL_TRANSMISSION
    int battery_low     = ( b[start_idx + 4] & 0x8 ) >> 3;
    int manual_transmit = ( b[start_idx + 4] & 0x2 ) >> 1;
    int seq             = ((b[start_idx + 6] << 8) | b[start_idx + 5]);

    double temp_c_int_array[3] = {999.0f};
    double temp_c_ext_array[3] = {999.0f};
    int humidity_array[3] = {999};

    if ((b[start_idx] == 0xA0) || (b[start_idx] == 0xA6) ) {
        /*
        A0 and A6: internal temperature + humidity
        |   Header          | Current frame   | Current frame-1 | Current frame-2 |
        LL ID_______ S COUNT T_INT HUMID OFFSE T_INT HUMID OFFSE T_INT HUMID OFFSE CRC32______
        1e a051fc6c2 2 0c 00 d9 00 22 01 05 00 d9 00 22 01 19 00 d7 00 2c 01 0d 00 cb 27 fa 60
        -1 0 1 2 3 4__ 5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28      <<< byte position in 'b' array + start_idx
        */
        temp_c_int_array[0] = decode_value(b,start_idx, 8, 0);
        temp_c_int_array[1] = decode_value(b,start_idx, 14, 0);
        temp_c_int_array[2] = decode_value(b,start_idx, 20, 0);
        humidity_array[0]   = decode_value(b, start_idx, 10, 0);
        humidity_array[1]   = decode_value(b, start_idx, 16, 0);
        humidity_array[2]   = decode_value(b, start_idx, 22, 0);

    } else if(b[start_idx] == 0xA3) {
        /*
        A3: internal + external temperature (normal range -40 - )
        |   Header          | Current Frame   | Current Frame -1| Current Frame -2|
        LL ID        S CNT__ T_INT T_EXT OFFSE T_INT T_EXT OFFSE T_INT T_EXT OFFSE CRC32______
        1e a3b02f727 2 34 19 e8 00 f4 00 3f 00 e2 00 f0 00 28 00 e0 00 e6 00 b8 00 b5 ab cc 2c
        -1 0 1 2 3 4__ 5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28       <<< byte position in 'b' array + start_idx
        */
        temp_c_int_array[0] = decode_value(b,start_idx, 8, 0);
        temp_c_int_array[1] = decode_value(b,start_idx, 14, 0);
        temp_c_int_array[2] = decode_value(b,start_idx, 20, 0);
        temp_c_ext_array[0]   = decode_value(b, start_idx, 10, 0);
        temp_c_ext_array[1]   = decode_value(b, start_idx, 16, 0);
        temp_c_ext_array[2]   = decode_value(b, start_idx, 22, 0);
    } else if(b[start_idx] == 0xA4) {
        /*
        A4: internal + external temperature (extended range) + humidity
        |   Header          | Current Frame         | Current Frame -1      | Current Frame -2      |
        LL ID        S CNT__ T_INT HUMID T_EXT OFFSE T_INT HUMID T_EXT OFFSE T_INT HUMID T_EXT OFFSE CRC32______
        24 a4903b641 0 5e 5f cf 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 6b f5 48 3c
        -1 0 1 2 3 4__ 5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34      <<< byte position in 'b' array + start_idx
        */
        temp_c_int_array[0] = decode_value(b, start_idx, 8, 1);
        temp_c_int_array[1] = decode_value(b, start_idx, 16, 1);
        temp_c_int_array[2] = decode_value(b, start_idx, 24, 1);
        humidity_array[0]   = decode_value(b, start_idx, 10, 0);
        humidity_array[1]   = decode_value(b, start_idx, 18, 0);
        humidity_array[2]   = decode_value(b, start_idx, 26, 0);
        temp_c_ext_array[0] = decode_value(b, start_idx, 12, 1);
        temp_c_ext_array[1] = decode_value(b, start_idx, 20, 1);
        temp_c_ext_array[2] = decode_value(b, start_idx, 28 ,1);
    } else if(b[start_idx] == 0xA5) {
        /*
        A5: internal temperature
        |   Header          | Cur Frame |  Frame -1 | Frame -2 |
        LL ID_______ S CNT__ T_INT OFSET T_INT OFSET T_INT OFSET CRC32______
        18 a529d7394 2 02 00 e3 00 08 00 e3 00 37 00 d9 00 02 00 85 7b 7d 7b
        -1 0 1 2 3 4__ 5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22       <<< byte position in 'b' array + start_idx
        */
        temp_c_int_array[0] = decode_value(b, start_idx, 8, 1);
        temp_c_int_array[1] = decode_value(b, start_idx, 12, 1);
        temp_c_int_array[2] = decode_value(b, start_idx, 16, 1);
    }

    /* clang-format off */
    data = data_make(
        "model",                 "",                    DATA_STRING, model_name,
        "id",                    "",                    DATA_STRING, id_str,
        "battery_ok",            "Battery OK",          DATA_INT,    !battery_low,
        "manual_transmit",       "Manual Transmit",     DATA_INT,    manual_transmit,
        "seq_number",            "Sequence Number",     DATA_INT,    seq,
        "temperature_C_int",     "Temperature int.",    DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c_int_array[0],
        "temperature_C_int_last","Temp. int. last",     DATA_ARRAY, data_array(3, DATA_DOUBLE, temp_c_int_array),
        "temperature_C_ext",     "Temperature ext.",    DATA_COND, temp_c_ext_array[0] < 999.0, DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c_ext_array[0],
        "temperature_C_ext_last","Temp. ext. last",     DATA_COND, temp_c_ext_array[0] < 999.0, DATA_ARRAY, data_array(3, DATA_DOUBLE, temp_c_ext_array),
        "humidity",              "Humidity",            DATA_COND, humidity_array[0] < 999, DATA_FORMAT, "%u %%", DATA_INT, humidity_array[0],
        "humidity_last",         "Humidity last",       DATA_COND, humidity_array[0]  < 999, DATA_ARRAY, data_array(3, DATA_INT, humidity_array),
        "mic",                   "Integrity",           DATA_STRING, "CRC",
        NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);
    return 1;
}


static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "manual_transmit",
        "seq_number",
        "temperature_C_int",
        "temperature_C_int_last",
        "temperature_C_ext",
        "temperature_C_ext_last",
        "humidity",
        "humidity_last",
        "mic",
        NULL,
};

r_device const tfa_30390X = {
        .name        = "TFA Dostmann 30.390X T/H sensors series",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 61,
        .long_width  = 61,
        .tolerance   = 5,
        .reset_limit = 3500,
        .decode_fn   = &tfa_30390X_decode,
        .fields      = output_fields,
};

