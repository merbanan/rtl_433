/** @file
    Chamberlain CWPIRC pir sensor.

    Copyright (C) 2023 Bruno OCTAU

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Chamberlain CWPIRC pir sensor.
Issue #2582 open by \@kuenkin

This is the webpage of the product itself: https://www.chamberlain.com/ca/cwp-wireless-motion-alert-add-on-sensor/p/CWPIRC

The pir sensor have a learn feature for pairing purpose with the base station up to 8 sensors.

Data layout :

    Byte position                00 01 02 03 04 05 06 07 08 09 10 11 12 13
        55 55 ... 55 55 55 2D D4 00 xx xx xx xx xx 01 yy yy yy yy yy CC CC
       |                  |     |                 |                 |     |
       |               ,--'     |                 |                 |     '--------,
       |Sync           |Preamble|Message 0        |Message 1        |CRC-16/XMODEM |

- Message 0   {48} 00 xx xx xx xx xx, always starting with 0x00
- Message 1   {48} 01 yy yy yy yy yy, always starting with 0x01
- CRC-16XModem{16} cc cc  from 00 to 11 byte

- Message 0 and 1 change regularly (every 30 / 35 minutes) , ID is not yet decoded from these 2 messages, tbd.
- Could be a rolling code and the learn feature could help to get the key ?
- In case of low battery the base emits a short beep, every 35 minutes. So the low battery information is coded into the 2 messages.
*/
static int chamberlain_cwpirc_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x55, 0x2D, 0xD4};

    if (bitbuffer->num_rows != 1) {
        decoder_logf(decoder, 2, __func__, "Expected 1 Row, here %d", bitbuffer->num_rows);
        return DECODE_ABORT_EARLY;
    }

    unsigned bits = bitbuffer->bits_per_row[0];

    if (bits < 136 ) {                 // too small
        decoder_logf(decoder, 2, __func__, "less than 136 bits, %d is too short", bits);
        return DECODE_ABORT_LENGTH;
    }

    unsigned pos = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof (preamble) * 8);

    if (pos >= bits) {
        decoder_logf(decoder, 2, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }

    data_t *data;
    uint8_t b[14];

    pos += sizeof(preamble) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, pos, b, sizeof(b) * 8);

    if (b[0] != 0 && b[6] != 1) {
        decoder_logf(decoder, 2, __func__, "Message 0 and 1 not found");
        return DECODE_ABORT_EARLY;
    }

    uint16_t crc_calc = crc16(b, 14, 0x1021, 0x0000);

    if (crc_calc != 0 ) {
        decoder_log(decoder, 1, __func__, "CRC error");
        return DECODE_FAIL_MIC;
    }

    char msg0[20], msg1[20];
    sprintf(msg0, "%02x%02x%02x%02x%02x", b[1], b[2], b[3], b[4], b[5]);
    sprintf(msg1, "%02x%02x%02x%02x%02x", b[7], b[8], b[9], b[10], b[11]);

    /* clang-format off */
    data = data_make(
        "model",   "Model",     DATA_STRING,   "Chamberlain-CWPIRC",
        "msg_0",   "Message 0", DATA_STRING,    msg0,
        "msg_1",   "Message 1", DATA_STRING,    msg1,
        "mic",     "Integrity", DATA_STRING,   "CRC",
        NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;

}

static char const *const output_fields[] = {
        "model",
        "msg_0",
        "msg_1",
        "mic",
        NULL,
};

r_device const chamberlain_cwpirc = {
        .name        = "Chamberlain CWPIRC PIR Sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 25,
        .long_width  = 25,
        .reset_limit = 500,
        .decode_fn   = &chamberlain_cwpirc_decode,
        .fields      = output_fields,
};
