/** @file
    Decoder for Gridstream RF devices produced by Landis & Gyr.

    Copyright (C) 2023 krvmk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/**
Landis & Gyr Gridstream Power Meters.

- Center Frequency: 915 Mhz
- Frequency Range: 902-928 MHz
- Channel Spacing: 100kHz, 300kHz
- Modulation: FSK-PCM (2-FSK, GFSK)
- Bitrates: 9600, 19200, 38400
- Preamble: 0xAAAA
- Syncword v4: 0b0000000001 0b0111111111
- Syncword v5: 0b0000000001 0b11111111111

This decoder is based on the information from: https://wiki.recessim.com/view/Landis%2BGyr_GridStream_Protocol
Datastream is variable length and bitrate depending on type fields
Preamble
Bytes after preamble are encoded with standard uart settings with start bit, 8 data bits and stop bit.
Data layouts:
    Subtype 55:
        AAAAAA SSSS TT YY LLLL KK BBBBBBBBBB WWWWWWWWWW II MMMMMMMM KKKK EEEEEEEE KKKK KKKKKK CCCC KKKK XXXX KK
    Subtype D2:
        AAAAAA SSSS TT YY LL K----------K XXXX
    Subtype D5:
        AAAAAA SSSS TT YY LLLL KK DDDDDDDD EEEEEEEE II K----------K CCCC KKKK XXXX
- A - Preamble
- S - Syncword
- T - Type
- Y - Subtype
- L - Length
- B - Broadcast
- D - Dest Address
- E - Source Address
- M - Uptime (time since last outage in seconds)
- I - Counter
- C - Clock
- K - Unknown
- X - CRC (poly 0x1021, init set by provider)

<<<<<<< Updated upstream
=======

>>>>>>> Stashed changes
*/

#include "decoder.h"
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

struct crc_init {
    uint16_t value;
    char const *location;
    char const *provider;
};
<<<<<<< Updated upstream
=======

/* 
Decoder will iterate through the known values until checksum is validated.

In order to identify new values, the reveng application, https://reveng.sourceforge.io/,
can determine a missing init value if given several fixed length packet streams. 
Subtype 0x55 with a data length of 0x23 can be used for this.

Known CRC init values can be added to the code via PR when they have been identified. 

*/
>>>>>>> Stashed changes
static struct crc_init known_crc_init[] = {
        {0xe623, "Kansas City, MO", "Evergy-Missouri West"},
        {0x5fd6, "Dallas, TX", "Oncor"},
        {0xD553, "Austin, TX", "Austin Energy"},
        {0x45F8, "Dallas, TX", "CoServ"},
        {0x62C1, "Quebec, CAN", "Hydro-Quebec"},
        {0x23D1, "Seattle, WA", "Seattle City Light"},
        {0x2C22, "Santa Barbara, CA", "Southern California Edison"},
        {0x142A, "Washington", "Puget Sound Energy"},
        {0x47F7, "Pennsylvania", "PPL Electric"},
        {0x22c6, "Long Island, NY", "PSEG Long Island"}};

static int gridstream_checksum(int fulllength, uint16_t length, uint8_t *bits, int adjust)
{
    uint16_t crc_count = 0;
    bool crc_ok        = false;
    uint16_t crc;

    if ((fulllength - 4 + adjust) < length) {
        return DECODE_ABORT_LENGTH;
    }
    crc = (bits[2 + length + adjust] << 8) | bits[3 + length + adjust];
    do {
        if (crc16(&bits[4 + adjust], length - 2, 0x1021, known_crc_init[crc_count].value) == crc) {
            crc_ok = true;
        }
        else {
            crc_count++;
        }
    } while (crc_count < (sizeof(known_crc_init) / sizeof(struct crc_init)) && crc_ok == false);
    if (!crc_ok) {
        return DECODE_FAIL_MIC;
    }
    else {
        return crc_count;
    }
}

static int gridstream_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t const preambleV4[] = {
            0xAA,
            0xAA,
            0x00,
            0x5F,
            0xF0,
    };
    uint8_t const preambleV5[] = {
            0xAA,
            0xAA,
            0x00,
            0x7F,
            0xF8,
    };
<<<<<<< Updated upstream
    uint8_t *b = malloc(bitbuffer->bits_per_row[0] / 8);
    uint16_t stream_len;
    char found_crc[5] = "";
    char destwanaddress_str[13];
    char srcwanaddress_str[13];
    char srcaddress_str[9];
    char destaddress_str[9];
    uint32_t uptime;
=======
    /* Match the size of the buffer b with the incoming bitbuffer size.
       As no known buffer size limit exists, this is the safest way to prevent
       buffer overflows.  */
    uint8_t *b = malloc(bitbuffer->bits_per_row[0] / 8);
    uint16_t stream_len;
    char found_crc[5] = "";
    char destwanaddress_str[13] = "";
    char srcwanaddress_str[13] = "";
    char srcaddress_str[9] = "";
    char destaddress_str[9] = "";
    uint32_t uptime = 0;
>>>>>>> Stashed changes
    time_t clock;
    char clock_str[80];
    int subtype;
    unsigned offset;
    int protocol_version;
    int subtype_mod = 0;
    int crcidx;
    int decoded_len;
    offset = bitbuffer_search(bitbuffer, 0, 0, preambleV4, 36);
    if (offset >= bitbuffer->bits_per_row[0]) {
        offset = bitbuffer_search(bitbuffer, 0, 0, preambleV5, 37);
        if (offset >= bitbuffer->bits_per_row[0]) {
            free(b);
            return DECODE_FAIL_SANITY;
        }
        else {
            decoded_len = extract_bytes_uart(bitbuffer->bb[0], offset + 37, bitbuffer->bits_per_row[0] - offset - 37, b);
            protocol_version = 5;
        }
    }
    else {
        decoded_len = extract_bytes_uart(bitbuffer->bb[0], offset + 36, bitbuffer->bits_per_row[0] - offset - 36, b);
        protocol_version = 4;
    }
    if (decoded_len >= 5) {
        switch (b[0]) {
        case 0x2A:
            subtype = b[1];
            if (subtype == 0xD2) {
                stream_len  = b[2];
                subtype_mod = -1;
            }
            else {
                stream_len = (b[2] << 8) | b[3];
            }
            crcidx = gridstream_checksum(decoded_len, stream_len, b, subtype_mod);
            if (crcidx < 0) {
                decoder_log(decoder, 1, __func__, "Bad CRC or unknown init value. ");
                if ((stream_len == 0x23) && (subtype = 0xAA)) {
                    decoder_log_bitrow(decoder, 1, __func__, &b[4], decoded_len * 8, "Use RevEng to find init value.");
                }
                free(b);
                return DECODE_FAIL_MIC;
            }
            sprintf(found_crc, "%04x", known_crc_init[crcidx].value);
            switch (subtype) {
            case 0x55:
                sprintf(destwanaddress_str, "%02x%02x%02x%02x%02x%02x", b[5], b[6], b[7], b[8], b[9], b[10]);
                sprintf(srcwanaddress_str, "%02x%02x%02x%02x%02x%02x", b[11], b[12], b[13], b[14], b[15], b[16]);
                sprintf(srcaddress_str, "%02x%02x%02x%02x", b[24], b[25], b[26], b[27]);
                uptime = ((uint32_t)b[18] << 24) | (b[19] << 16) | (b[20] << 8) | b[21];
<<<<<<< Updated upstream

                /* clang-format off */
                        data = data_make(
                                "model",        "",                 DATA_STRING,    "LandisGyr-GS",
                                "networkID",    "Network ID",       DATA_STRING,    found_crc,
                                "location",     "Location",         DATA_STRING,    known_crc_init[crcidx].location,
                                "provider",     "Provider",         DATA_STRING,    known_crc_init[crcidx].provider,
                                "id",           "Source Meter ID",  DATA_STRING,    srcaddress_str,
                                "subtype",      "",                 DATA_INT,       subtype,
                                "wanaddress",   "",                 DATA_STRING,    srcwanaddress_str,
                                "destaddress",  "",                 DATA_STRING,    destwanaddress_str,
                                "uptime",       "",                 DATA_INT,       uptime,
                                "protoversion",  "",                 DATA_INT,       protocol_version,
                                "mic",          "Integrity",        DATA_STRING,    "CRC",
                                NULL);
                /* clang-format on */

                decoder_output_data(decoder, data);
                break;
            case 0xD2:

                /* clang-format off */
                        data = data_make(
                                "model",        "",             DATA_STRING, "LandisGyr-GS",
                                "networkID",    "Network ID",   DATA_STRING, found_crc,
                                "location",     "Location",     DATA_STRING, known_crc_init[crcidx].location,
                                "provider",     "Provider",     DATA_STRING, known_crc_init[crcidx].provider,
                                "subtype",      "",             DATA_INT,    subtype,
                                "protoversion", "",             DATA_INT,    protocol_version,
                                "mic",          "Integrity",    DATA_STRING, "CRC",
                                NULL);
                /* clang-format on */

                decoder_output_data(decoder, data);
=======
>>>>>>> Stashed changes
                break;
            case 0xD5:
                sprintf(destaddress_str, "%02x%02x%02x%02x", b[5], b[6], b[7], b[8]);
                sprintf(srcaddress_str, "%02x%02x%02x%02x", b[9], b[10], b[11], b[12]);
                if (stream_len == 0x47) {
                    clock  = ((uint32_t)b[14] << 24) | (b[15] << 16) | (b[16] << 8) | b[17];
                    uptime = ((uint32_t)b[22] << 24) | (b[23] << 16) | (b[24] << 8) | b[25];
                    sprintf(srcwanaddress_str, "%02x%02x%02x%02x%02x%02x", b[30], b[31], b[32], b[33], b[34], b[35]);
                    strftime(clock_str, sizeof(clock_str), "%a %Y-%m-%d %H:%M:%S %Z", localtime(&clock));
<<<<<<< Updated upstream

                    /* clang-format off */
                            data = data_make(
                                    "model",        "",                 DATA_STRING, "LandisGyr-GS",
                                    "networkID",    "Network ID",       DATA_STRING, found_crc,
                                    "location",     "Location",         DATA_STRING, known_crc_init[crcidx].location,
                                    "provider",     "Provider",         DATA_STRING, known_crc_init[crcidx].provider,
                                    "id",           "Source Meter ID",  DATA_STRING, srcaddress_str,
                                    "subtype",      "",                 DATA_INT,    subtype,
                                    "destaddress",  "Target Meter ID",  DATA_STRING, destaddress_str,
                                    "timestamp",    "",                 DATA_STRING, clock_str,
                                    "uptime",       "",                 DATA_INT,    uptime,
                                    "wanaddress",   "",                 DATA_STRING, srcwanaddress_str,
                                    "protoversion", "",                 DATA_INT,   protocol_version,
                                    "mic",          "Integrity",        DATA_STRING, "CRC",
                                    NULL);
                    /* clang-format on */
                }
                else {

                    /* clang-format off */
                            data = data_make(
                                    "model",        "",                 DATA_STRING, "LandisGyr-GS",
                                    "networkID",    "Network ID",       DATA_STRING, found_crc,
                                    "location",     "Location",         DATA_STRING, known_crc_init[crcidx].location,
                                    "provider",     "Provider",         DATA_STRING, known_crc_init[crcidx].provider,
                                    "id",           "Source Meter ID",  DATA_STRING, srcaddress_str,
                                    "subtype",      "",                 DATA_INT,    subtype,
                                    "destaddress",  "Target Meter ID",  DATA_STRING, destaddress_str,
                                    "protoversion", "",                 DATA_INT,   protocol_version,
                                    "mic",          "Integrity",        DATA_STRING, "CRC",
                                    NULL);
                    /* clang-format on */
                }

                decoder_output_data(decoder, data);
                break;
            }
            break;
        default:
            free(b);
            return DECODE_ABORT_LENGTH;
=======
                }
                break;
            }

            /* clang-format off */
            data = data_make(
                "model",        "",                     DATA_STRING,    "LandisGyr-GS",
                "networkID",    "Network ID",           DATA_STRING,    found_crc,
                "location",     "Location",             DATA_STRING,    known_crc_init[crcidx].location,
                "provider",     "Provider",             DATA_STRING,    known_crc_init[crcidx].provider,
                "subtype",      "",                     DATA_INT,       subtype,
                "protoversion", "",                     DATA_INT,       protocol_version,
                "mic",          "Integrity",            DATA_STRING,    "CRC",
                "id",           "Source Meter ID",      DATA_COND,      subtype != 0xD2, DATA_STRING, srcaddress_str,
                "wanaddress",   "Source Meter WAN ID",  DATA_COND,      srcwanaddress_str != "", DATA_STRING, srcwanaddress_str,
                "destaddress",  "Target Meter WAN ID",  DATA_COND,      subtype == 0x55, DATA_STRING, destwanaddress_str,
                "destaddress",  "Target Meter ID",      DATA_COND,      subtype == 0xD5, DATA_STRING, destaddress_str,
                "timestamp",    "Timestamp",            DATA_COND,      subtype == 0xD5 && stream_len == 0x47, DATA_STRING, clock_str,
                "uptime",       "Uptime",               DATA_COND,      uptime > 0, DATA_INT, uptime,
                NULL);
            /* clang-format on */
            
>>>>>>> Stashed changes
            break;
        }
        decoder_log_bitrow(decoder, 0, __func__, b, decoded_len * 8, "Decoded frame data");
        free(b);
        // Return 1 if message successfully decoded
        return 1;
    }
    else {
        free(b);
        return DECODE_FAIL_SANITY;
    }
}

static char const *const output_fields[] = {
        "model",
        "networkID",
        "location",
        "provider",
        "id",
        "subtype",
        "wanaddress",
        "destaddress",
        "uptime",
        "srclocation",
        "destlocation",
        "timestamp",
        "protoversion",
        "framedata",
        "mic",
        NULL,
};

r_device const gridstream96 = {
        .name        = "Gridstream decoder 9.6k",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 104,
        .long_width  = 104,
        .reset_limit = 20000,
        .decode_fn   = &gridstream_decode,
        .disabled    = 0,
        .fields      = output_fields,
};

r_device const gridstream192 = {
        .name        = "Gridstream decoder 19.2k",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,
        .long_width  = 52,
        .reset_limit = 20000,
        .decode_fn   = &gridstream_decode,
        .disabled    = 0,
        .fields      = output_fields,
};

r_device const gridstream384 = {
        .name        = "Gridstream decoder 38.4k",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 22,
        .long_width  = 22,
        .reset_limit = 20000,
        .decode_fn   = &gridstream_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
