/** @file
    Oil tank monitor using manchester encoded FSK protocol with CRC.

    Copyright (C) 2022 Christian W. Zuckschwerdt <zany@triq.net>
    Device analysis by StarMonkey1
    Improved device mapping by havochome

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/

#include "decoder.h"

/**
Oil tank monitor using manchester encoded FSK protocol with CRC.

Tested devices:
- Apollo Ultrasonic Smart liquid monitor (FSK, 433.92M) Issue #2244

Should apply to similar Watchman, Beckett, and Apollo devices too.

There is a preamble plus de-sync of 555558, then MC coded an inner preamble of 5558 (raw 9999996a).
End of frame is the last half-bit repeated additional 2 times, then 4 times mark.

The sensor sends a single packet once every half hour to 33 mins or twice a second
for 5 minutes when in pairing/test mode, 13 mins when filling up or alarming.

Depth reading is in cm, lowest reading appears to be 4cm, highest is supposed to be 3m
but readings of 310 have been observed; invalid depth is 0cm.

Data Format:

    ID:32h FIXED:b TXSTATUS:b TEMP_OK:2b FIXED:b BAT:b SENSOR?2b COUNTER:4b unknown:3b DEPTH_CM:9d CRC:8h

Data Layout:

    ID ID ID ID DATA1 DATA2 DATA3 CRC
    B0 B1 B2 B3   B4    B5    B6  B7

- ID: 32 bit Sensor Identity (B0, B1, B2, and B3)
- DATA1: Status Flags (B4)
- DATA2: Counter, unknown, and MSB for Depth (B5)
- DATA3: Depth in cm (B6)
- CRC: CRC-8, poly 0x31 init 0x00, bit reflected (B7)

DATA 1:
- Fixed: B4 bit 8 (0x80) fixed 0
- TxStatus: B4 bit 7 (0x40), 0 = noral transmit (every 30 to 33 mins), 1 = every 0.5 to 1 second during binding/alarm/refueling
- Temp1: B4 bit 6 (0x20), Too cold to operate when = B4 bit 5 (0x10)
- Temp2: B4 bit 5 (0x10), Too cold to operate when = B4 bit 6 (0x20)
- Fixed: B4 bit 4 (0x08) fixed 0 - or could be like temp and working with B4 bit 3 (0x04)
- Battery: B4 bits 3 (0x04) could be battery ok TODO this could be the same as bits 6/5 above working with B4 bit 4 (0x08)
- Sensor?: B4 bit 2 (0x02) works opposite to bit 1 (0x01) - like temp, so sensor?
- Sensor?: B4 bit 1 (0x01) what happens when B4 bit 1 (0x01) = bit 2 (0x02)?

DATA2:
- Fixed: B5 bit 8 (0x80) fixed 0
- Counter: B5 bits 7 to 5 (0x40 - 0x10) counts up and down over 24hrs - probably used for day marker in usage stats
    Counter also appears to have a weekly marker and possibly a 4 weekly marker - again for stats?
- Mode B:  B5 bits 4 - 2 (0x08 - 0x02) unknown
- Depth: B5 bit 1 (0x01) MSB for depth

DATA3:
 - Depth: Depth in cm (nominally 4cm - 300cm) depth reading of 0cm is error - no reading

Alarm appears to be TxStatus in 'rapid' mode and depth change of greater than 1.5 cm - this appears to be a
function of the receiver and Alarm does not appear to be coded by transmitter.

example packets are:

- raw: {158}555558 9999 996a 6559aaa99996a55696a9a5963c - original
       {158}555558 a955 5569 5a9aaa56a996966aa69596a63c - my sensor
- aligned: {134}9999996a 6559aaa999969aa6aa9a6995 fc
- decoded: 5558 bd01 5642 0497 - original
           1ff9 c40e 1668 2762 - my sensor

TODO: this is not confirmed
Start of frame full preamble is depending on first data bit either

    0101 0101 0101 0101 0101 0111 01
    0101 0101 0101 0101 0101 1000 10
*/
static int oil_smart_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{

    bitbuffer_t databits = {0};
    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &databits, 64);

    if (databits.bits_per_row[0] < 64) {
        return 0; // DECODE_ABORT_LENGTH; // TODO: fix calling code to handle negative return values
    }

    uint8_t *b = databits.bb[0];

    if (crc8le(b, 8, 0x31, 0x00)) {
        decoder_log(decoder, 2, __func__, "CRC8 fail");
        return 0; // DECODE_FAIL_MIC; // TODO: fix calling code to handle negative return values
    }

    // Unit ID does NOT changes when you force TxStatus: Rapid
    // by holding a magnet to the sensor for long enough
    // 32 bit sensor ID is stable
    uint32_t unit_id = ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3]);

    // TxStatus - 0 normal 30/33 mins repeat tx, 1 rapid 0.5/1 second repeat
    // TxStatus - B4 bit 7 (0x40)
    char *txstatus;
    if (b[4] & 0x40) {
        txstatus = "Rapid";
    }
    else {
        txstatus = "Normal";
    }

    // temp_ok - warm enough to work 1 - too cold 0
    // Temperature B4 bit 6 (0x20)= B4 bit 5 (0x10) to cold to work
    uint8_t temp_ok = 1;  //temp ok by default
    if ((b[4] & 0x10) == (b[4] & 0x20)) {
        temp_ok = 0;
    }

    // battery level OK - 1 OK - 0 low?
    // battery_ok - B4 bit 3 (0x04)
    uint8_t battery = (b[4] & 0x04) >> 2;

    // Appears to be sensor working with bit 2 (0x02) and bit 1 (0x01)
    // sensor - B4 bit 2 (0x02) and B4 bit 1 (0x01)
    uint8_t sensor = (b[4] & 0x03);

    // Counter goes up and down over 24hrs refecting number of reading being sent
    // Counter - B5 bits 8 - 5 (0x80 - 0x10)
    uint8_t counter = (b[5] & 0xf0) >> 4;

    // Unknown - just capturing
    // unknown - B5 bits 4 - 2 (0x04 - 0x02). Bit 2 could be fixed 0
    uint8_t unknown = (b[5] & 0x0d) >> 1;

    // Bad reading is zero depth

    // depth in cm msb B5 bit 1 (0x01) and B6
    uint16_t depth = ((b[5] & 0x01) << 8) + b[6];

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Oil-Ultrasonic",
            "id",               "",                 DATA_FORMAT, "%08x", DATA_INT, unit_id,
            "depth_cm",         "Depth",            DATA_INT,    depth,
            "txstatus",         "TxStatus",         DATA_STRING, txstatus,
            "temp_ok",          "temp_ok",          DATA_INT,    temp_ok,
            "battery_ok",       "Battery Level",    DATA_INT,    battery,
            "sensor",           "Sensor?",          DATA_INT,    sensor,
            "counter",          "Counter",          DATA_INT,    counter,
            "unknown",          "unknown",          DATA_INT,    unknown,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
Oil tank monitor using manchester encoded FSK protocol with CRC.
@sa oil_smart_decode()
*/
static int oil_smart_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[2] = {0x55, 0x58};
    // End of frame is the last half-bit repeated additional 2 times, then 4 times mark.

    unsigned bitpos = 0;
    int events      = 0;

    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 16)) + 128 <=
            bitbuffer->bits_per_row[0]) {
        events += oil_smart_decode(decoder, bitbuffer, 0, bitpos + 16);
        bitpos += 2;
    }

    return events;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "depth_cm",
        "txstatus",
        "temp_ok",
        "battery_ok",
        "sensor",
        "counter",
        "unknown",
        "mic",
        NULL,
};

r_device const oil_smart = {
        .name        = "Oil Ultrasonic SMART FSK",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 500,
        .long_width  = 500,
        .gap_limit   = 2000,
        .reset_limit = 9000,
        .decode_fn   = &oil_smart_callback,
        .fields      = output_fields,
};
