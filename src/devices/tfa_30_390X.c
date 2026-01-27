/** @file
    
    TFA Dostmann 30.3901.02 temperature sensor.
    TFA Dostmann 30.3902.02 temperature sensor with external sensor.
    TFA Dostmann 30.3905.02 temperature and humidity sensor with external temp sensor.
    TFA Dostmann 30.3906.02 temperature and humidity sensor
    TFA Dostmann 30.3908.02 temperature and humidity sensor (big display)

    Copyright (c) 2026 Jacob Maxa <jack77@gmx.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    
*/

/*  @fn int  tfa_30390X_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    
    All sensors work on 868.025 Mhz with 250 kHz sample rate/bandwidth
    
    This device is part of the ID+ sensor system with tfa.me cloud ability.

    Depending on the sensors capability the type following the preamble is:
    - 0x18 - internal temperature only (but seems that humidity is also transmitted), devices 30.3901.02
    - 0x1e - internal temperature and humidity, devices 30.390
    - 0x24 - internal temperature, humidity and external temperature

    ## Example for message ID 0x24 (30.3905.02):
    All packets starting with a 0xaaaaaaaa SYNC followed by a 0x4b2dd42b preamble followed by type id 0x24

                     | Current Frame        | Current Frame -1      | Current Frame -2      |
    ID        S CNT__ T_INT HUMID T_EXT ????? T_INT HUMID T_EXT ????? T_INT HUMID T_EXT ????? CRC32______
    a4903b641 0 5e 5f cf 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 6b f5 48 3c 0000
    a4903b641 0 5f 5f ce 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 db 36 70 5a 0000*
    a4903b641 0 60 5f cf 00 68 01 c6 00 3c 00 ce 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 3e 7f 50 dd 0000
    a4903b641 0 61 5f cf 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 ce 00 68 01 c6 00 3c 00 a6 48 8f 58 0000
    a4903b641 0 62 5f cf 00 68 01 c6 00 3c 01 9e 00 d0 03 8c 00 78 01 9e 00 d0 03 8c 00 78 01 81 4a e4 cc 0000 <<< invalid packet
    a4903b641 0 63 5f ce 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 70 66 4a 00 000
    a4903b641 0 64 5f ce 00 68 01 c6 00 3c 00 ce 00 68 01 c6 00 3c 00 cf 00 68 01 c6 00 3c 00 c7 8c e1 76 0000
    a4903b641 0 65 5f ce 00 68 01 c6 00 3c 00 ce 00 68 01 c6 00 3c 00 ce 00 68 01 c6 00 3c 00 ce 2a 56 5d 0000
    a4903b641 0 66 5f ce 00 68 01 c6 00 3c 00 ce 00 68 01 c6 00 3c 00 ce 00 68 01 c6 00 3c 00 36 c7 01 af 0000
    a4903b641 0 68 5f ce 00 68 01 c7 00 3c 00 ce 00 68 01 c6 00 3c 00 ce 00 68 01 c6 00 3c 00 7d 0c 91 a0 000
    a4903b641 0 69 5f ce 00 68 01 c7 00 3c 00 ce 00 68 01 c7 00 3c 00 ce 00 68 01 c6 00 3c 00 85 e6 29 dc 0000
    0 1 2 3 4__ 5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34      <<< byte position in 'b' array

    - ID - unique 9 byte long address of the sensor, printed on its back
    - S  - status nibble (0xMXBX)
        - M - manual forced transmission by button press on device ( = 1)
        - B - battery OK flag; 0 = batt OK, 1 = batt low
    - CNT - Up-counter to detect missing packets (2 byte, little-endian)
    - T_INT - internal device temperature  (2 byte, little-endian), must be divided by 10
    - HUMID - humidity (2 byte, little-endian),  must be divided by 10
    - T_EXT - see T_INT
    - CRC32 - checksum, see below

    ## Example for 0x1e (30.3902.02):
    All packets starting with a 0xaaaaaaaa SYNC followed by a 0x4b2dd42b preamble followed by type id 0x1e

                     | Current Frame   | Current Frame -1| Current Frame -2|
    ID        S CNT__ T_INT T_EXT ????? T_INT T_EXT ????? T_INT T_EXT ????? CRC32______
    a3b02f727 2 34 19 e8 00 f4 00 3f 00 e2 00 f0 00 28 00 e0 00 e6 00 b8 00 b5 ab cc 2c 000047
    a3b02f727 2 f4 19 e9 00 e2 00 03 01 ee 00 e2 00 2c 01 ff 00 e5 00 2c 01 3a df e7 f2 000042 --> Tint = 23,5, Text = 22,7
    a3b02f727 2 f5 19 e9 00 e2 00 01 00 e9 00 e2 00 03 01 ee 00 e2 00 2c 01 a6 89 eb aa 0000c6
    a3b02f727 2 f6 19 e9 00 e2 00 01 00 e9 00 e2 00 01 00 e9 00 e2 00 03 01 70 c0 09 ee 000003
    a3b02f727 0 e2 1c dd 00 2e 07 2c 01 de 00 22 07 2c 01 dd 00 2d 07 2c 01 ec 7c 74 15 0000
    0 1 2 3 4__ 5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28

    #####
    Example for 0x18 (30.3901.02):
    All packets starting with a 0xaaaaaaaa SYNC followed by a 0x4b2dd42b preamble followed by type id 0x1e
    #####
                     | Current Frame   | Current Frame -1| Current Frame -2|
    ID        S CNT__ T_INT HUMID T_INT HUMID T_INT HUMID CRC32______
    a529d7394 0 10 00 cf 00 2c 01 d4 00 2c 01 df 00 2c 01 af f4 c3 9a 000040 
    a529d7394 0 11 00 cd 00 2c 01 cf 00 2c 01 d4 00 2c 01 3a 21 27 1d 0000408
    a529d7394 0 12 00 cb 00 2c 01 cd 00 2c 01 cf 00 2c 01 e2 76 46 4b 000040


    CRC-32 parameters:
    for TFA 30.3905.02: width=32  poly=0x04c11db7  init=0xfa848a5b  refin=true  refout=true  xorout=0x00000000  check=0x83cd1cad  residue=0x00000000  name=(none)
    for TFA 30.3902.02: width=32  poly=0x04c11db7  init=0x152ea23e  refin=true  refout=true  xorout=0x00000000  check=0x30480572  residue=0x00000000  name=(none)

     The sensors send the current value followed by the last two values in case a packet was lost.
     In combination with the counter value lost packets can be recovered.

*/

#include "decoder.h"
#include "logger.h"

/*#define TFA_303905_MESSAGE_BITLEN       320
#define TFA_303905_CRC32_POLYNOM        0x04c11db7
#define TFA_303905_CRC32_INIT           0xfa848a5b
#define TFA_303905_PREAMBLE_ID          (0x24)


#define TFA_303902_MESSAGE_BITLEN       312
#define TFA_303902_CRC32_INIT           0x152ea23e
#define TFA_303902_PREAMBLE_ID          (0x1e)
*/
#define TFA_30390X_ID_TINT              (0x18)
#define TFA_30390X_ID_TINT_HUMID        (0x1e)
#define TFA_30390X_ID_TINT_HUMID_TEXT   (0x24)

#define TFA_30390X_MESSAGE_MIN_BITLEN   320

#define TFA_30390X_CRC32_POLY_REFLECTED 0xEDB88320u  /* reflected default CRC32 0x04C11DB7 */
#define TFA_30390X_CRC32_INIT           0xFFFFFFFFu
#define TFA_30390X_CRC32_XOROUT         0xFFFFFFFFu

/* *
 * Bitwise reflection of a byte for the crc32 calculation
 * */
static uint8_t reflect8(uint8_t x) {
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) {
        if (x & (1u << i))
            r |= 1u << (7 - i);
    }
    return r;
}

/* *
 * Bitwise reflection of a double word for the crc32 calculation
 * */
static uint32_t reflect32(uint32_t x) {
    uint32_t r = 0;
    for (int i = 0; i < 32; i++) {
        if (x & (1u << i))
            r |= 1u << (31 - i);
    }
    return r;
}

/* *
 * CRC32 checksum calculation, should return 0x0 when CRC value is present in byte array if checksum succeeds
 * CRC32 nach RevEng:
 * width=32
 * poly=0x04C11DB7
 * init=0xFFFFFFFF
 * refin=true
 * refout=true
 * xorout=0x11111111
 *
 * */


uint32_t crc32_reveng(const uint8_t *data, size_t length)
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

static float decode_value(uint8_t* b, size_t start_idx, size_t offs_hi) {
    uint16_t raw = ((uint16_t)b[start_idx + offs_hi] << 8) |    (uint16_t)b[start_idx + offs_hi - 1];
    int16_t val = 0;
    if(b[start_idx - 1] == TFA_303905_PREAMBLE_ID) { 
        val = (int16_t)(raw << 4) >> 4; // 12-Bit sign-extended
    } else if(b[start_idx - 1] == TFA_303902_PREAMBLE_ID) {
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
    bitbuffer_extract_bytes(bitbuffer, 0, bitpos, b, TFA_30390X_MESSAGE_MIN_BITLEN);

    
    printf("Extracted data: ");
    for (i = 0 ; i < 40; i++) {
        printf("%02x", b[i]);
    }
    printf("\n");
    uint32_t crc32_calculated = 0;
    uint32_t crc32_frame      = 0;
    // check CRC32 checksum depending on sensor type
    if(b[start_idx-1] == TFA_30390X_ID_TINT_HUMID_TEXT) {
        crc32_calculated = crc32_reveng(&b[start_idx], 30);
        crc32_frame = ((uint32_t)b[start_idx + 35] << 24) | ((uint32_t)b[start_idx + 34] << 16) | ((uint32_t)b[start_idx + 33] << 8) | b[start_idx + 32];
        
    } else if(b[start_idx-1] == TFA_30390X_ID_TINT_HUMID) {
        if (crc32_reveng(&b[start_idx], 35  )) {
            print_logf(LOG_DEBUG, __func__, "TFA 30.3905.02 CRC32 failed");
            return DECODE_FAIL_MIC;
        }
    } else if(b[start_idx-1] == TFA_30390X_ID_TINT) {
        if (crc32_reveng(&b[start_idx], 35  )) {
            print_logf(LOG_DEBUG, __func__, "TFA 30.3905.02 CRC32 failed");
            return DECODE_FAIL_MIC;
        }
    } else {
        return DECODE_FAIL_OTHER;
    }

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
        print_logf(LOG_DEBUG, __func__, "Invalid ID");
        return DECODE_FAIL_SANITY;
    }
    
    char id_str[30] = {0};

    // build human readable ID string
    sprintf(id_str, "%02X%02X%02X%02X%02X", b[start_idx + 0], b[start_idx + 1], b[start_idx + 2], b[start_idx + 3], b[start_idx + 4]);
    // remove last character as it is the status byte
    id_str[9] = 0;
    // status nibble : bit 3 LOW BATTERY, bit 1 MANUAL_TRANSMISSION
    int battery_low     = ( b[start_idx + 4] & 0x8 ) >> 3;
    int manual_transmit = ( b[start_idx + 4] & 0x2 ) >> 1;
    int seq             = ((b[start_idx + 6] << 8) | b[start_idx + 5]);


    float temp_c_int, temp_c_int_1, temp_c_int_2    = 0.0f;
    int humidity, humidity_1, humidity_2            = 999.0f; // only required for TFA 30.3905
    float temp_c_ext, temp_c_ext_1, temp_c_ext_2    = 999.0f; // 999.0 degC as default to dectect if set during evaluation as the value could never be real
    double temp_c_int_array[3] = {0};
    double temp_c_ext_array[3] = {0};
    
    // read T_int from packet, with history values
    tmp  = ((b[start_idx + 8] << 8) | b[start_idx + 7]);
    if (tmp & 0x800) {
        temp_c_int = (int16_t)(0xf000 | tmp) / 10.0f; // extend 12 bit to 16 bit signed int
    } else {
        temp_c_int    = tmp / 10.0f;
    }

    temp_c_int = decode_value(b, start_idx, 8);

    if (b[start_idx-1] == TFA_30390X_ID_TINT_HUMID_TEXT) {

    }
    else if (b[start_idx-1] == TFA_30390X_ID_TINT_HUMID) {

    } 
    else if (b[start_idx-1] == TFA_30390X_ID_TINT) {

    }


    switch(b[start_idx-1]) {
        case TFA_303905_PREAMBLE_ID:
            humidity        = decode_value(b, start_idx, 10);// ((b[start_idx + 10] << 8) | b[start_idx + 9]) / 10;
            humidity_1      = decode_value(b, start_idx, 18);// ((b[start_idx + 18] << 8) | b[start_idx + 17]) / 10;
            humidity_2      = decode_value(b, start_idx, 26); // ((b[start_idx + 26] << 8) | b[start_idx + 25]) / 10;


            temp_c_int_1 = decode_value(b, start_idx, 16);
            temp_c_int_2 = decode_value(b, start_idx, 24);
            temp_c_ext = decode_value(b, start_idx, 12);
            temp_c_ext_1 = decode_value(b, start_idx, 20);
            temp_c_ext_2 = decode_value(b, start_idx, 28);

            temp_c_int_array[0]  = temp_c_int;
            temp_c_int_array[1]  = temp_c_int_1;
            temp_c_int_array[2]  = temp_c_int_2;
            
            temp_c_ext_array[0] = temp_c_ext;
			temp_c_ext_array[1] = temp_c_ext_1;
            temp_c_ext_array[2] = temp_c_ext_2;
			
            int humidity_array [] = {humidity, humidity_1, humidity_2};

            /* clang-format off */
            data = data_make(
                "model",                 "",                    DATA_STRING, "TFA-30.390X.02",
                "id",                    "",                    DATA_STRING, id_str,
                "battery_ok",            "Battery OK",          DATA_INT,    !battery_low,
                "manual_transmit",       "Manual Transmit",     DATA_INT,    manual_transmit,
                "seq_number",            "Sequence Number",     DATA_INT,    seq,
                "temperature_C_int",     "Temperature int.",    DATA_FORMAT, "%.1f °C", DATA_DOUBLE, temp_c_int,
                "temperature_C_int_last","Temp. int. last",     DATA_ARRAY, data_array(3, DATA_DOUBLE, temp_c_int_array),
                //"energy_kWh",       "Energy",           DATA_COND,   itotal != 0, DATA_FORMAT, "%.2f kWh",DATA_DOUBLE, total_energy,
                "temperature_C_ext"      "Temperature ext.",    DATA_COND ,temp_c_ext < 999.0, DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c_ext,
                //"temperature_C_ext",     "Temperature ext.",    DATA_FORMAT, "%.1f °C", DATA_DOUBLE, temp_c_ext,
                "temperature_C_ext"      "Temperature ext.",    DATA_COND ,temp_c_ext < 999.0, DATA_ARRAY, data_array(3, DATA_DOUBLE, temp_c_ext_array),
                //"temperature_C_ext_last","Temp. ext. last",     DATA_ARRAY, data_array(3, DATA_DOUBLE, temp_c_ext_array),
                "humidity",              "Humidity",            DATA_COND, humidity < 999.0, DATA_FORMAT, "%u %%", DATA_INT, humidity,
                //"humidity",              "Humidity",            DATA_FORMAT, "%u %%", DATA_INT, humidity,
                "humidity_last",         "Humidity last",       DATA_COND, humidity < 999.0,DATA_ARRAY, data_array(3, DATA_INT, humidity_array),
                //"humidity_last",         "Humidity last",       DATA_ARRAY, data_array(3, DATA_INT, humidity_array),
                "mic",                   "Integrity",           DATA_STRING, "CRC",
                NULL);
            /* clang-format on */
            break;
			
        case TFA_303902_PREAMBLE_ID:		
			temp_c_ext = decode_value(b, start_idx, 10);
			temp_c_int_1 = decode_value(b, start_idx, 14);
            temp_c_int_2 = decode_value(b, start_idx, 20);
            temp_c_ext_1 = decode_value(b, start_idx, 16);
            temp_c_ext_2 = decode_value(b, start_idx, 22);			
		
            temp_c_int_array[0]  = temp_c_int;
			temp_c_int_array[1]  = temp_c_int_1;
			temp_c_int_array[2]  = temp_c_int_2;
			
            temp_c_ext_array[0] = temp_c_ext;
			temp_c_ext_array[1] = temp_c_ext_1;
            temp_c_ext_array[2] = temp_c_ext_2;
			
            /* clang-format off */
            data = data_make(
                "model",                 "",                    DATA_STRING, "TFA-30.390X.02",
                "id",                    "",                    DATA_STRING, id_str,
                "battery_ok",            "Battery OK",          DATA_INT,    !battery_low,
                "manual_transmit",       "Manual Transmit",     DATA_INT,    manual_transmit,
                "seq_number",            "Sequence Number",     DATA_INT,    seq,
                "temperature_C_int",     "Temperature int.",    DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c_int,
				"temperature_C_int_last","Temp. int. last",     DATA_ARRAY, data_array(3, DATA_DOUBLE, temp_c_int_array),
                "temperature_C_ext",     "Temperature ext.",    DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c_ext,
				"temperature_C_ext_last","Temp. ext. last",     DATA_ARRAY, data_array(3, DATA_DOUBLE, temp_c_ext_array),
                "mic",                   "Integrity",           DATA_STRING, "CRC32",
                NULL);
            /* clang-format on */
            break;
    }

    /* clang-format off */
    data = data_make(
        "model",                 "",                    DATA_STRING, "TFA-30.390X.02",
        "id",                    "",                    DATA_STRING, id_str,
        "battery_ok",            "Battery OK",          DATA_INT,    !battery_low,
        "manual_transmit",       "Manual Transmit",     DATA_INT,    manual_transmit,
        "seq_number",            "Sequence Number",     DATA_INT,    seq,
        "temperature_C_int",     "Temperature int.",    DATA_FORMAT, "%.1f °C", DATA_DOUBLE, temp_c_int,
        "temperature_C_int_last","Temp. int. last",     DATA_ARRAY, data_array(3, DATA_DOUBLE, temp_c_int_array),
        //"energy_kWh",       "Energy",           DATA_COND,   itotal != 0, DATA_FORMAT, "%.2f kWh",DATA_DOUBLE, total_energy,
        "temperature_C_ext"      "Temperature ext.",    DATA_COND ,temp_c_ext < 999.0, DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c_ext,
        //"temperature_C_ext",     "Temperature ext.",    DATA_FORMAT, "%.1f °C", DATA_DOUBLE, temp_c_ext,
        "temperature_C_ext"      "Temperature ext.",    DATA_COND ,temp_c_ext < 999.0, DATA_ARRAY, data_array(3, DATA_DOUBLE, temp_c_ext_array),
        //"temperature_C_ext_last","Temp. ext. last",     DATA_ARRAY, data_array(3, DATA_DOUBLE, temp_c_ext_array),
        "humidity",              "Humidity",            DATA_COND, humidity < 999.0, DATA_FORMAT, "%u %%", DATA_INT, humidity,
        //"humidity",              "Humidity",            DATA_FORMAT, "%u %%", DATA_INT, humidity,
        "humidity_last",         "Humidity last",       DATA_COND, humidity < 999.0,DATA_ARRAY, data_array(3, DATA_INT, humidity_array),
        //"humidity_last",         "Humidity last",       DATA_ARRAY, data_array(3, DATA_INT, humidity_array),
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
        .name        = "TFA Dostmann 30.390X T/H sensors series ",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 61,
        .long_width  = 61,
        .tolerance   = 5, 
        .reset_limit = 1600,
        .decode_fn   = &tfa_30390X_decode,
        .fields      = output_fields,
        .gap_limit   = 1500,
};

