/** @file
    Decoder for TBH Archos devices.

    Copyright (c) 2019 duc996 <duc_996@gmx.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */
/**
Decoder for devices from the TBH project (https://www.projet-tbh.fr)

- Modulation: FSK PCM
- Frequency: 433.93MHz +-10kHz
- 212 us symbol/bit time

There exist several device types (power, meteo, gaz,...)

Payload format:
- Synchro           {32} 0xaaaaaaaa
- Preamble          {32} 0xd391d391
- Length            {8}
- Payload           {n}
- Checksum          {16} CRC16 poly=0x8005 init=0xffff

To get raw data:

    ./rtl_433 -f 433901000 -X n=tbh,m=FSK_PCM,s=212,l=212,r=3000

The application data is obfuscated by doing data[n] xor data[n-1] xor info[n%16].

Payload foramt:
- Device id         {32}
- Frame type        {8}
- Frame Data        {x}

Frame types:
- Raw data      1
- Weather       2
- Battery level 3
- Battery low   4

Weather frame format:
- Type        {8} 02
- Temperature {16} unsigned in 0.1 Celsius steps
- Humidity    {16} unsigned rel%

Raw data frame (power index):
- Version {8}
- Index     {24}
- Timestamp {34}
- MaxPower  {16}
- some additinal data ???
- CRC8 poly=0x7 the crc includes a length byte at the beginning
*/

#include "decoder.h"

static int archos_tbh_decode(r_device *decoder, bitbuffer_t *bitbuffer)
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

    if (len > 60) {
        if (decoder->verbose)
            fprintf(stderr, "%s: packet to large (%d bytes), drop it\n", __func__, len);
        return DECODE_ABORT_LENGTH;
    }

    uint8_t frame[62] = {0}; //TODO check max size, I have no idea, arbitrary limit of 60 bytes + 2 bytes crc
    frame[0] = len;
    // Get frame (len don't include the length byte and the crc16 bytes)
    bitbuffer_extract_bytes(bitbuffer, row,
            start_pos + (sizeof (preamble) + 1) * 8,
            &frame[1], (len + 2) * 8);

    if (decoder->verbose > 1) {
        bitrow_printf(frame, (len + 1) * 8, "%s: frame data: ", __func__);
    }

    uint16_t crc = crc16(frame, len + 1, 0x8005, 0xffff);

    if ((frame[len + 1] << 8 | frame[len + 2]) != crc) {
        if (decoder->verbose) {
            fprintf(stderr, "%s: CRC invalid %04x != %04x\n", __func__,
                    frame[len + 1] << 8 | frame[len + 2], crc);
        }
        return DECODE_FAIL_MIC;
    }

    uint8_t const info[] = {
            0x19, 0xF8, 0x28, 0x30, 0x6d, 0x0c, 0x94, 0x54,
            0x22, 0xf2, 0x37, 0xc9, 0x66, 0xa3, 0x97, 0x57
    };

    uint8_t payload[62] = {0};

    payload[0] = frame[1] ^ info[0];
    for (int i = 1; i < len; ++i) {
        payload[i] = frame[i] ^ frame[i + 1] ^ info[i % sizeof (info)];
    }
    if (decoder->verbose > 1) {
        bitrow_printf(payload, len * 8, "%s: frame data: ", __func__);
    }

    uint8_t type = payload[4];
    uint32_t id  = payload[0] | payload[1] << 8 | payload[2] << 16 | (uint32_t)(payload[3]) << 24;

    if (type == 1) {
        // raw data
        if (decoder->verbose)
            fprintf(stderr, "%s: raw data from ID: %08x\n", __func__, id);

        payload[4] = len - 4; //write len for crc (len - 4b ID)

        if (decoder->verbose > 1) {
            bitrow_printf(&payload[4], (len - 4) * 8, "%s: data: ", __func__);
        }

        uint8_t c = crc8(&payload[4], len - 5, 0x07, 0x00);

        if (c != payload[len - 1]) {
            fprintf(stderr, "%s: crc error\n", __func__);
            return DECODE_FAIL_MIC;
        }

        uint32_t idx      = payload[6] << 16 | payload[7] << 8 | payload[8];
        uint32_t ts       = payload[9] << 16 | payload[10] << 8 | payload[11];
        uint32_t maxPower = payload[12] << 8 | payload[13];

        if (decoder->verbose > 1)
            fprintf(stderr, "%s: index: %d, timestamp: %d, maxPower: %d\n", __func__,
                    idx, ts, maxPower);

        /* clang-format off */
        data = data_make(
                "model",        "",                 DATA_STRING, "Archos-TBH",
                "id",           "Station ID",       DATA_FORMAT, "%08X", DATA_INT, id,
                "power_idx",    "Power index",      DATA_FORMAT, "%d", DATA_INT, idx,
                "power_max",    "Power max",        DATA_FORMAT, "%d", DATA_INT, maxPower,
                "timestamp",    "Timestamp",        DATA_FORMAT, "%d s", DATA_INT, ts / 8,
                "mic",          "Integrity",        DATA_STRING, "CRC",
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (type == 2) {
        // temp and humidity
        int temp_raw = (payload[6] << 8 | payload[5]) - 2732;
        float temp_c = temp_raw * 0.1;
        int humidity = payload[7];

        /* clang-format off */
        data = data_make(
                "model",        "",                 DATA_STRING, "Archos-TBH",
                "id",           "Station ID",       DATA_FORMAT, "%08X", DATA_INT, id,
                "temperature_C", "Temperature",     DATA_FORMAT, "%.01f °C", DATA_DOUBLE, temp_c,
                "humidity",     "Humidity",         DATA_FORMAT, "%d %%", DATA_INT, humidity,
                "mic",          "Integrity",        DATA_STRING, "CRC",
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (type == 3) {
        // bat level, 0-100%
        int batt_level = payload[5];

        /* clang-format off */
        data = data_make(
                "model",        "",                 DATA_STRING, "Archos-TBH",
                "id",           "Station ID",       DATA_FORMAT, "%08X", DATA_INT, id,
                "battery_ok",   "Battery level",    DATA_FORMAT, "%0.2f", DATA_DOUBLE, batt_level * 0.01,
                "mic",          "Integrity",        DATA_STRING, "CRC",
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (type == 4) {
        // battery low

        /* clang-format off */
        data = data_make(
                "model",        "",                 DATA_STRING, "Archos-TBH",
                "id",           "Station ID",       DATA_FORMAT, "%08X", DATA_INT, id,
                "battery_ok",   "Battery level",    DATA_INT,    0, // fixed
                "mic",          "Integrity",        DATA_STRING, "CRC",
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else {
        if (decoder->verbose)
            fprintf(stderr, "%s: unknown frame received\n", __func__);
        return DECODE_FAIL_SANITY;
    }
}

static char *output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "humidity",
        "power_idx",
        "power_max",
        "timestamp",
        "mic",
        NULL,
};

r_device archos_tbh = {
        .name        = "TBH weather sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 212,
        .long_width  = 212,
        .reset_limit = 3000,
        .decode_fn   = &archos_tbh_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
