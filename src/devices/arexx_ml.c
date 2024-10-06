/** @file
    Arexx Multilogger.

    Copyright (C) 2023 Christian W. Zuckschwerdt <zany@triq.net>
    Protocol analysis by MacH-21, TSN-70E by inonoob.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Arexx Multilogger.

- Arexx IP-HA90 (MCP9808 sensor) s.a. #2388
- Arexx IP-TH78EXT
- Arexx TSN-70E (Sensirion SHT-10 sensor) s.a. #2482

The IP-HA90 has a Microchip RFPIC12f675f at 433.92M and a Microchip MCP9808 temperature sensor.
The TSN-70E has a Sensirion SHT-10 temperature and humidity and temperature sensor.

FSK modulated with Manchester encoding, half-bit width is 208 us (2400bps MC).
The sensors transmit approx. every 45 seconds alternating Temperature/Humidity.
Polarity is inverted (IEEE MC) and the preamble+sync is aaaaaaaa55.

Integrity check is done using CRC8 using poly=0x31 init=0x00.

Example raw messages:

    55555555aa f8 71fe fedf f777 5b a4  ff
    55555555aa f8 71fe fedf f727 80 7f  ff
    55555555aa f8 71fe fedf f6e7 8e 71  ff
    55555555aa f8 71fe fedf f69f b4 4b  ff
    55555555aa f8 71fe fedf f66f c0 3f  ff
    55555555aa f8 71fe fedf f63f 1b e4  ff
    55555555aa f8 71fe fedf f61f 38 c7  ff
    55555555aa f8 71fe fedf f607 67 98  ff
    55555555aa f8 71fe fedf f5f7 46 b9  ff
    55555555aa f8 71fe fedf f5d7 65 9a  ff
    55555555aa f8 71fe fedf f5b7 00 ff  ff
    55555555aa f8 71fe fedf f59f e1 1e  ff
    55555555aa fa 15b2 e90f 6c ff  faf7 7b1c e3
    55555555aa fa 14b2 f90e 51 ff  faf7 7b1a e41
    55555555aa fa 14b2 f991 01 ff  faf7 7b2a 401
    55555555aa fa 15b2 e678 0f ff  faf7 7bf8 41

Message format (preamble 5555aa then invert):

    LEN:8h ID:<16h SENS:16h ?:8h8h CHK:8h CHKINV:8h 16x

Message layout:

    LL IIII SSSS UUUU XX YY

- L : 8 bit: message length 7 or 5 (including length byte, excluding checksum)
- I : 16 bit: ID, little-endian, even number = Temperature
- S : 16 bit: raw sensor value
- U : 16 bit: optional extra data, unknown
- X : 8 bit: CRC, poly 0x31, init 0xc0
- Y : 8 bit: inverted CRC check, only IP-HA90

*/

static int arexx_ml_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xaa, 0xaa, 0x55}; // 24 bits

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY; // we expect a single row
    }
    if (bitbuffer->bits_per_row[0] < 64 || bitbuffer->bits_per_row[1] > 130) {
        return DECODE_ABORT_EARLY; // we expect around 88 to 104 bits
    }
    bitbuffer_invert(bitbuffer);

    int msg_len = -1;
    uint8_t b[9]; // allow up to 9 byte messages
    for (int i = 0; i < bitbuffer->num_rows; ++i) {
        unsigned pos = bitbuffer_search(bitbuffer, i, 0, preamble, 24);
        pos += 24;

        if (pos + 64 > bitbuffer->bits_per_row[i])
            continue; // too short or not found

        bitbuffer_extract_bytes(bitbuffer, i, pos, b, sizeof(b) * 8);
        msg_len = b[0];
        break;
    }
    if (msg_len <= 0) {
        decoder_log(decoder, 2, __func__, "Couldn't find preamble");
        return DECODE_FAIL_SANITY;
    }

    int chk = crc8le(b, msg_len, 0x31, 0x00);
    if (chk != b[msg_len]) { // || (chk ^ b[msg_len + 1]) != 0) {
        decoder_log(decoder, 2, __func__, "CRC fail");
        return DECODE_FAIL_MIC;
    }

    // Extract data from buffer
    int id       = (b[2] << 8) | (b[1]); // little-endian
    int sens_val = (b[3] << 8) | (b[4]); // big-endian?
    int is_humi  = id & 1; // even number: Temperature, odd number: Humidity

    // MCP9808 Ambient Temperature Register "5-4":
    int temp_alert = (sens_val >> 13) & 7;
    int temp_raw   = (int16_t)(sens_val << 3); // uses sign-extend
    float temp_c   = temp_raw / 128;

    if (msg_len == 5) { // not sure if this is the proper check
        // SHT10 Temperature
        temp_c = sens_val * 0.01f - 40.0f; // offset actually varies by Vdd
    }
    // SHT10 Humidity
    float humidity = -2.0468 + 0.0367 * sens_val - 1.5955E-6 * sens_val * sens_val;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Arexx-ML",
            "id",               "ID",               DATA_FORMAT, "%04x",    DATA_INT, id,
            "temperature_C",    "Temperature",      DATA_COND, !is_humi,    DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
            "temperature_alert",  "Alert",          DATA_COND, !is_humi,    DATA_FORMAT, "%x", DATA_INT, temp_alert,
            "humidity",         "Humidity",         DATA_COND, is_humi,     DATA_FORMAT, "%.1f %%", DATA_DOUBLE, humidity,
            "sensor_raw",       "Sensor Raw",       DATA_FORMAT, "%04x",    DATA_INT, sens_val,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const arexx_ml_output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "temperature_alert",
        "humidity",
        "sensor_raw",
        "mic",
        NULL,
};

r_device const arexx_ml = {
        .name        = "Arexx Multilogger IP-HA90, IP-TH78EXT, TSN-70E",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 208, // 2400bps MC
        .long_width  = 208, // not used
        .reset_limit = 450,
        .decode_fn   = &arexx_ml_decode,
        .fields      = arexx_ml_output_fields,
};
