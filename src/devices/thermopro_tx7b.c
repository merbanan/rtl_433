/** @file
    ThermoPro TX-7B Outdoor Thermometer Hygrometer.

    Copyright (C) 2025 Bruno OCTAU (\@ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ThermoPro TX-7B Outdoor Thermometer Hygrometer.

- Outdor Sensor with Temperature and Hymidity
- Compatible with ThermoPro TP260B/TP280B stations.
- Issue #3306
- Product web page : https://buythermopro.com/product/tx7b/
- Very similar protocol and data layout with ThermoPro TP829b

Flex decoder:

    rtl_433 -X "n=tx7b,m=FSK_PCM,s=108,l=108,r=1500,preamble=d2552dd4,bits>=160" 2>&1 | grep codes

    codes: {124}e800293017aa55aa83d2d2d2d2d200
    codes: {124}25202ca00daa55aabbd2d2d2d2d200

Data layout:

    Byte Position              0  1  2  3  4  5  6  7  8  9 10 11 12 13 14
    Sample        d2 55 2d d4 e8 00 29 30 17 aa 55 aa 83 d2 d2 d2 d2 d2 00
    Sample        d2 55 2d d4 25 20 2c a0 0d aa 55 aa bb d2 d2 d2 d2 d2 00
                              II BF 11 12 22 aa 55 aa CC TT TT TT TT TT TT
                                 X
                                 C

- II:  {8} Sensor ID,
- B:   {1} Low Battery = 1, Normal Battery = 0
- X:   {1} TX Button , 1 = pressed for immediate rf transmit.
- C:   {2} Channel offset -1, 0x0 = CH 1, 0x1 = CH 2, 0x2 = CH 3 (3 sensors max are supported by station)
- F:   {4} Unknown flags, always 0x0,
- 111:{12} Temperature, °C, offset 400, scale 10,
- 222:{12} Humidity, %,
- aa55aa:{24} fixed value 0xaa55aa
- CC:  {8} Checksum, Galois Bit Reflect Byte Reflect, gen 0x98, key 0x25, final XOR 0x00,
- TT:      Trailed bytes, not used (always d2 d2 d2 d2 d2 00 ...).

*/
static int thermopro_tx7b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = { // 0xd2, removed to increase success
                                        0x55, 0x2d, 0xd4};

    uint8_t b[9];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    int msg_len = bitbuffer->bits_per_row[0];

    if (msg_len > 260) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    int offset = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= msg_len) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    if ((msg_len - offset ) < 96 ) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 9 * 8);

    // checksum is a Galois bit reflect and byte reflect, gen 0x98, key 0x25, final XOR 0x00
    int checksum = lfsr_digest8_reverse(b, 8, 0x98, 0x25);

    if (checksum != b[8]) {
        decoder_logf(decoder, 1, __func__, "Checksum error, calculated %x, expected %x", checksum, b[8]);
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 2, __func__, b, 72, "MSG");

    int id        = b[0];
    int channel   = ((b[1] & 0x30) >> 4) + 1;
    int low_bat   = b[1] >> 7;
    int tx_button = (b[1] & 0x40) >> 6;
    int flags     = b[1] & 0xF;
    int temp_raw  = b[2] << 4 | (b[3] & 0xF0) >> 4;
    int humidity  = b[4];
    float temp    = (temp_raw - 400) * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",          "",             DATA_STRING, "ThermoPro-TX7B",
            "id",             "",             DATA_FORMAT, "%02x",    DATA_INT,    id,
            "battery_ok",     "Battery",      DATA_INT,    !low_bat,
            "button",         "Button",       DATA_INT,    tx_button,
            "channel",        "Channel",      DATA_INT,    channel,
            "flags",          "Flags",        DATA_FORMAT, "%04b",    DATA_INT,    flags,
            "temperature_C",  "Temperature",  DATA_FORMAT, "%.1f C",  DATA_DOUBLE, temp,
            "humidity",       "Humidity",     DATA_FORMAT, "%d %%",   DATA_INT, humidity,
            "mic",            "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const tx7b_output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "button",
        "channel",
        "flags",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device const thermopro_tx7b = {
        .name        = "ThermoPro TX-7B Outdoor Thermometer Hygrometer",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 108,
        .long_width  = 108,
        .reset_limit = 1500,
        .decode_fn   = &thermopro_tx7b_decode,
        .fields      = tx7b_output_fields,
};
