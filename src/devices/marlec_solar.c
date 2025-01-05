/** @file
    Decoder for Marlec Solar iBoost+ devices.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */
/**
Decoder for Marlec Solar iBoost+ devices.

Note: work in progress, very similar to Archos-TBH.

- Modulation: FSK PCM
- Frequency: 868.3MHz
- 20 us bit time
- based on TI CC1100

Payload format:
- Preamble          {32} 0xaaaaaaaa
- Syncword          {32} 0xd391d391
- Length            {8}
- Payload           {n}
- Checksum          {16} CRC16 poly=0x8005 init=0xffff

Usual payload lengths seem to be 37 (0x25), 105 (0x69), 66 (0x42).

To get raw data:

    ./rtl_433 -f 868.3M -X 'n=Marlec,m=FSK_PCM,s=20,l=20,g=350,r=600,preamble=aad391d391'
*/

#include "decoder.h"

static int marlec_solar_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {
            /*0xaa, 0xaa, */ 0xaa, 0xaa, // preamble
            0xd3, 0x91, 0xd3, 0x91       // sync word
    };

    data_t *data;

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int row = 0;
    // Validate message and reject it as fast as possible : check for preamble
    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, sizeof (preamble) * 8);

    if (start_pos == bitbuffer->bits_per_row[row]) {
        return DECODE_ABORT_EARLY; // no preamble detected
    }

    // check min length
    if (bitbuffer->bits_per_row[row] < 12 * 8) { //sync(4) + preamble(4) + len(1) + data(1) + crc(2)
        return DECODE_ABORT_LENGTH;
    }

    uint8_t len;
    bitbuffer_extract_bytes(bitbuffer, row, start_pos + sizeof (preamble) * 8, &len, 8);

    // usual lengths seem to be 37 (0x25), 105 (0x69), 66 (0x42)
    if (len > 105) {
        decoder_logf(decoder, 1, __func__, "packet to large (%d bytes), drop it", len);
        return DECODE_ABORT_LENGTH;
    }

    uint8_t frame[108] = {0}; // arbitrary limit of 1 len byte + 105 data bytes + 2 bytes crc
    frame[0] = len;
    // Get frame (len doesn't include the length byte or the crc16 bytes)
    bitbuffer_extract_bytes(bitbuffer, row,
            start_pos + (sizeof (preamble) + 1) * 8,
            &frame[1], (len + 2) * 8);

    decoder_log_bitrow(decoder, 2, __func__, frame, (len + 1) * 8, "frame data");

    uint16_t crc = crc16(frame, len + 1, 0x8005, 0xffff);

    if ((frame[len + 1] << 8 | frame[len + 2]) != crc) {
        decoder_logf(decoder, 1, __func__, "CRC invalid %04x != %04x",
                frame[len + 1] << 8 | frame[len + 2], crc);
        return DECODE_FAIL_MIC;
    }

    int const SAVED_TODAY     = 0xCA;
    int const SAVED_YESTERDAY = 0xCB;
    int const SAVED_LAST_7    = 0xCC;
    int const SAVED_LAST_28   = 0xCD;
    int const SAVED_TOTAL     = 0xCE;

    int frame_type  = frame[3];
    // if (frame[3] == 0x22) {
    int boost_time  = frame[6]; // boost time remaining (minutes)
    int solar_off   = frame[7];
    int tank_hot    = frame[8];
    int battery_low = frame[13];
    int heating     = (int16_t)((frame[17]) | (frame[18] << 8));
    int import_val  = (frame[19]) | (frame[20] << 8) | (frame[21] << 16) | (frame[22] << 24);
    int saved_type  = frame[25];
    int saved_val   = (frame[26]) | (frame[27] << 8) | (frame[28] << 16) | (frame[29] << 24);
    //}

    char frame_str[sizeof(frame) * 2 + 1]   = {0};
    for (int i = 0; i < len; ++i)
        sprintf(&frame_str[i * 2], "%02x", frame[i + 1]);

    int is_data = frame_type == 0x22;
    /* clang-format off */
    data = NULL;
    data = data_str(data, "model",            "",             NULL,         "Marlec-Solar");
    if (is_data) {
        data = data_int(data, "boost_time",       "",             NULL,         boost_time);
    }
    if (is_data) {
        data = data_int(data, "solar_off",        "",             NULL,         solar_off);
    }
    if (is_data) {
        data = data_int(data, "tank_hot",         "",             NULL,         tank_hot);
    }
    if (is_data) {
        data = data_int(data, "battery_low",      "",             NULL,         battery_low);
    }
    if (is_data) {
        data = data_int(data, "heating",          "",             NULL,         heating);
    }
    if (is_data) {
        data = data_int(data, "import_val",       "",             NULL,         import_val);
    }
    if (is_data && saved_type == SAVED_TODAY) {
        data = data_int(data, "saved_today",      "",             NULL,         saved_val);
    }
    if (is_data && saved_type == SAVED_YESTERDAY) {
        data = data_int(data, "saved_yesterday",  "",             NULL,         saved_val);
    }
    if (is_data && saved_type == SAVED_LAST_7) {
        data = data_int(data, "saved_last_7",     "",             NULL,         saved_val);
    }
    if (is_data && saved_type == SAVED_LAST_28) {
        data = data_int(data, "saved_last_28",    "",             NULL,         saved_val);
    }
    if (is_data && saved_type == SAVED_TOTAL) {
        data = data_int(data, "saved_total",      "",             NULL,         saved_val);
    }
    data = data_str(data, "raw",              "Raw data",     NULL,         frame_str);
    data = data_str(data, "mic",              "Integrity",    NULL,         "CRC");
    /* clang-format on */
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "boost_time",
        "solar_off",
        "tank_hot",
        "battery_low",
        "heating",
        "import_val",
        "saved_today",
        "saved_yesterday",
        "saved_last_7",
        "saved_last_28",
        "saved_total",
        "raw",
        "mic",
        NULL,
};

r_device const marlec_solar = {
        .name        = "Marlec Solar iBoost+ sensors",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 20,
        .long_width  = 20,
        .reset_limit = 300,
        .decode_fn   = &marlec_solar_decode,
        .fields      = output_fields,
};
