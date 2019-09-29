/** @file
    TFA Dostmann 30.3196 T/H outdoor sensor.

    Copyright (c) 2019 Christian W. Zuckschwerdt <zany@triq.net>
    Documented by Ekkehart Tessmer.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
TFA Dostmann 30.3196 T/H outdoor sensor at 868.33M.

https://www.tfa-dostmann.de/en/produkt/temperature-humidity-transmitter-11/
https://clientmedia.trade-server.net/1768_tfadost/media/7/86/3786.pdf

The device comes with 'TFA Modus Plus' (indoor) base station.
Up to three outdoor sensors can be operated (ch 1, 2, or 3).

- At the start there is a 6 ms gap (FSK space)
- Data is Manchester coded with a half-bit width of 245 us
- The data row is repeated four times with 7 ms gaps (FSK space)

- A second layer of manchester coding yields 16 bit preamble and 48 bits data
- The 64 bits of preamble 0xcccccccccccccccc, after first MC 0xaaaaaaaa, after second MC 0xffff
- A data row consists of 48 bits (6 Bytes).

Data layout:

    FFFFFFFF ??CCTTTT TTTTTTTT BHHHHHHH AAAAAAAA AAAAAAAA

- F: 8 bit Fixed message type 0xA8. d2d2d333 -> 9995 -> 57 (~ A8)
- C: 2 bit Channel number (1,2,3,X)
- T: 12 bit Temperature (Celsius) offset 40 scaled 10
- B: 1 bit Low battery indicator
- H: 7 bit Humidity
- A: 16 bit LFSR hash, gen 0x8810, key 0x22d0
- e.g. TYPE:8h ?2h CH:2d TEMP:12d BATT:1b HUM:7d CHK?16h

Example data:

    a8 21 fa 5b 38 54 : 10101000 00100001 11111010 01011011 00111000 01010100
    a8 22 22 5e 90 48 : 10101000 00100010 00100010 01011110 10010000 01001000

*/

#include "decoder.h"

static int tfa_303196_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0x55, 0x56}; // 12 bit preamble + 4 bit data
    int row;
    data_t *data;
    uint8_t *b;
    bitbuffer_t databits = {0};

    row = bitbuffer_find_repeated_row(bitbuffer, 2, 48 * 2 + 12); // expected are 4 rows, require 2
    if (row < 0)
        return DECODE_ABORT_EARLY;

    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, 16);
    start_pos += 12; // skip preamble

    if (bitbuffer->bits_per_row[row] - start_pos < 48 * 2)
        return DECODE_ABORT_LENGTH; // short buffer or preamble not found

    bitbuffer_manchester_decode(bitbuffer, row, start_pos, &databits, 48);

    if (databits.bits_per_row[0] < 48)
        return DECODE_ABORT_LENGTH; // payload malformed MC

    b = databits.bb[0];

    if (b[0] != 0xa8)
        return DECODE_FAIL_SANITY;

    uint32_t chk_data = ((unsigned)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3]);
    uint16_t digest   = (b[4] << 8) | (b[5]);
    int chk           = lfsr_digest16(chk_data, 32, 0x8810, 0x22d0) ^ digest;

    //bitrow_printf(b, 48, "TFA-303196 (%08x  %04x  %04x): ", chk_data, digest, session);

    int channel     = (b[1] >> 4) + 1;
    int temp_raw    = ((b[1] & 0x0F) << 8) | b[2];
    float temp_c    = temp_raw * 0.1 - 40;
    int battery_low = b[3] >> 7;
    int humidity    = b[3] & 0x7F;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "TFA-303196",
            "id",               "",             DATA_INT,    chk,
            "channel",          "Channel",      DATA_INT,    channel,
            "battery_ok",       "Battery",      DATA_INT,    !battery_low,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",              "Integrity",    DATA_STRING, "missing",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device tfa_303196 = {
        .name        = "TFA Dostmann 30.3196 T/H outdoor sensor",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 245,
        .long_width  = 0, // unused
        .tolerance   = 60,
        .reset_limit = 22000,
        .decode_fn   = &tfa_303196_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
