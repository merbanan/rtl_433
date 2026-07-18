/** @file
    LaCrosse WS6868 weather station sensors (TX231RW, TX232TH-LCD).

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
LaCrosse WS6868 weather station sensors (TX231RW wind/rain, TX232TH-LCD temperature/humidity).

Reverse engineered in issue #3120. FSK_PCM at 58 us/bit (not the 100 us
initially assumed), with a fixed 32 bit preamble/sync `0xd2aa2dd4`
(found by bit-shifting captures for a byte boundary, not a literal
on-air preamble -- the raw preamble is longer alternating bits).

Common header after the preamble, both sensors:

    ID:24h BAT:1b TEST:1b CHANNEL:2b COUNTER:3b ?:1b

- ID: 24 bit, did not change across a power cycle in the single unit
  tested (unconfirmed whether that's normal or this unit's quirk)
- CHANNEL: 0-based, add 1 to match the sensor's physical channel dial
- COUNTER: increments by 1 every transmission (seen as a raw nibble
  incrementing by 2, i.e. this 3 bit counter left-shifted by the
  trailing spare bit)

Split into two decoders below: the two sensors' frames differ in byte
layout and length, and their post-preamble bit lengths overlap in real
captures, so length can't tell them apart -- only their own CRC can. Each
decoder here only has to deal with its own frame.
*/

#define WS6868_PREAMBLE_BITLEN 32

static uint8_t const ws6868_preamble[4] = {0xd2, 0xaa, 0x2d, 0xd4};

// Common header fields, same layout for both sensors.
static void ws6868_parse_header(uint8_t const *b, uint32_t *id, int *battery_low, int *test, int *channel, int *counter)
{
    *id          = ((uint32_t)b[0] << 16) | (b[1] << 8) | b[2];
    *battery_low = (b[3] >> 7) & 1;
    *test        = (b[3] >> 6) & 1;
    *channel     = (b[3] >> 4) & 3;
    *counter     = (b[3] >> 1) & 7;
}

/** @fn static int lacrosse_ws6868_tx232th_decode(r_device *decoder, bitbuffer_t *bitbuffer)
TX232TH-LCD (temperature/humidity), 8 byte frame after the header above:

    TEMP:12d HUM:12d CRC:8h

- TEMP: scaled by 10, offset 500 (same convention as LaCrosse TX141, see
  lacrosse_tx141x.c)
- HUM: percent, plain integer
- CRC: CRC-8 poly 0x31 init 0x00 over the preceding 7 bytes

Verified against 10 real captures with the sensor's own displayed
temperature/humidity as ground truth (issue #3120, \@ProfBoc75):
digit-for-digit match on both temperature and humidity, and CRC valid,
for every sample.
*/
static int lacrosse_ws6868_tx232th_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int pos = bitbuffer_search(bitbuffer, 0, 0, ws6868_preamble, WS6868_PREAMBLE_BITLEN);
    if (pos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }
    pos += WS6868_PREAMBLE_BITLEN;

    if (bitbuffer->bits_per_row[0] - pos < 64) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[8];
    bitbuffer_extract_bytes(bitbuffer, 0, pos, b, 64);

    uint8_t crc = crc8(b, 7, 0x31, 0x00);
    if (crc != b[7]) {
        decoder_logf(decoder, 1, __func__, "CRC invalid %02x != %02x", crc, b[7]);
        return DECODE_FAIL_MIC;
    }

    uint32_t id;
    int battery_low, test, channel, counter;
    ws6868_parse_header(b, &id, &battery_low, &test, &channel, &counter);

    int temp_raw = (b[4] << 4) | (b[5] >> 4);
    int humidity = ((b[5] & 0x0f) << 8) | b[6];
    float temp_c = (temp_raw - 500) * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",             DATA_STRING, "LaCrosse-TX232TH",
            "id",            "",             DATA_FORMAT, "%06x", DATA_INT, id,
            "channel",       "Channel",      DATA_INT,    channel + 1,
            "battery_ok",    "Battery",      DATA_INT,    !battery_low,
            "test",          "Test",         DATA_INT,    test,
            "counter",       "Counter",      DATA_INT,    counter,
            "temperature_C", "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",      "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",           "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const lacrosse_ws6868_tx232th_output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "test",
        "counter",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device const lacrosse_ws6868_tx232th = {
        .name        = "LaCrosse WS6868 TX232TH-LCD temperature/humidity sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 2000,
        .decode_fn   = &lacrosse_ws6868_tx232th_decode,
        .fields      = lacrosse_ws6868_tx232th_output_fields,
};

/** @fn static int lacrosse_ws6868_tx231rw_decode(r_device *decoder, bitbuffer_t *bitbuffer)
TX231RW (wind/rain), 12 byte frame after the header above:

    ?:48b CRC:8h CHECKSUM:8h

- The 6 bytes between the header and CRC are unknown: every capture
  available (12 real captures, no wind or rain occurring during any of
  them) has them fixed at the same value, so there's no way to locate
  wind speed/direction or rainfall fields from this data -- reported
  as data_raw instead of guessed at
- CRC: CRC-8 poly 0x31 init 0x00 over the preceding 10 bytes
- CHECKSUM: sum of the preceding 11 bytes (including the CRC byte), & 0xff
*/
static int lacrosse_ws6868_tx231rw_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int pos = bitbuffer_search(bitbuffer, 0, 0, ws6868_preamble, WS6868_PREAMBLE_BITLEN);
    if (pos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }
    pos += WS6868_PREAMBLE_BITLEN;

    if (bitbuffer->bits_per_row[0] - pos < 96) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[12];
    bitbuffer_extract_bytes(bitbuffer, 0, pos, b, 96);

    uint8_t crc      = crc8(b, 10, 0x31, 0x00);
    uint8_t checksum = add_bytes(b, 11) & 0xff;
    if (crc != b[10] || checksum != b[11]) {
        decoder_logf(decoder, 1, __func__, "CRC/checksum invalid %02x/%02x != %02x/%02x",
                crc, checksum, b[10], b[11]);
        return DECODE_FAIL_MIC;
    }

    uint32_t id;
    int battery_low, test, channel, counter;
    ws6868_parse_header(b, &id, &battery_low, &test, &channel, &counter);

    char data_raw[13];
    for (int i = 0; i < 6; ++i) {
        snprintf(&data_raw[i * 2], 3, "%02x", b[4 + i]);
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",             DATA_STRING, "LaCrosse-TX231RW",
            "id",            "",             DATA_FORMAT, "%06x", DATA_INT, id,
            "channel",       "Channel",      DATA_INT,    channel + 1,
            "battery_ok",    "Battery",      DATA_INT,    !battery_low,
            "test",          "Test",         DATA_INT,    test,
            "counter",       "Counter",      DATA_INT,    counter,
            "data_raw",      "Undecoded data", DATA_STRING, data_raw,
            "mic",           "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const lacrosse_ws6868_tx231rw_output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "test",
        "counter",
        "data_raw",
        "mic",
        NULL,
};

r_device const lacrosse_ws6868_tx231rw = {
        .name        = "LaCrosse WS6868 TX231RW wind/rain sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 2000,
        .decode_fn   = &lacrosse_ws6868_tx231rw_decode,
        .fields      = lacrosse_ws6868_tx231rw_output_fields,
};
