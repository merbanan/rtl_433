/** @file
    LaCrosse Technology View LTV-R1, LTV-R3 Rainfall Gauge, LTV-W1/W2 Wind Sensor.

    Copyright (C) 2020 Mike Bruski (AJ9X) <michael.bruski@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
LaCrosse Technology View LTV-R1, LTV-R3 Rainfall Gauge, LTV-W1/W2 Wind Sensor.

Product pages:
https://www.lacrossetechnology.com/products/ltv-r1
https://www.lacrossetechnology.com/products/724-2310

Specifications:
- Rainfall 0 to 9999.9 mm

No internal inspection of the sensors was performed so can only
speculate that the remote sensors utilize a HopeRF CMT2119A ISM
transmitter chip which is tuned to 915Mhz.

No internal inspection of the console was performed but if the above
assumption is true, then the console most likely uses the HopeRF
CMT2219A ISM receiver chip.

(http://www.cmostek.com/download/CMT2119A_v0.95.pdf)
(http://www.cmostek.com/download/CMT2219A.pdf)
(http://www.cmostek.com/download/AN138%20CMT2219A%20Configuration%20Guideline.pdf)

Protocol Specification:

Data bits are NRZ encoded with logical 1 and 0 bits 104us in length.

Checksum is CRC-8 poly 0x31 init 0x00 over 7 (10 for R3) bytes following SYNC.

Note that the rain zero value seems to be `00aa00` with a known byte order of `HH??LL`.
It's unknown if the 16-bit value would reset or roll over into the middle byte (with whitening)?

## LTV-R1:

Full preamble is `fff00000 aaaaaaaa d2aa2dd4`.

    PRE:32h SYNC:32h ID:24h ?:4b SEQ:3d ?:1b RAIN:24h CRC:8h CHK?:8h TRAILER:96h

    {164} 380322  0e  00aa14  6a  93  00...
    {164} 380322  00  00aa1a  60  81  00...
    {162} 380322  06  00aa26  d1  04  00...

## LTV-R3:

Does not have the CRC at byte 8 but a second 24 bit value and the check at byte 11.
Full preamble is `aaaaaaaaaaaaaa d2aa2dd4`.

    PRE:58h SYNC:32h ID:24h ?:4b SEQ:3d ?:1b RAIN:24h RAIN:24h CRC:8h TRAILER:56h

    {144} 71061d 42 00aa00 00aa00  c6  0000000000000000 [zero]
    {144} 71061d 08 00aac3 00aab7  01  0000000000000000 [before 8-bit rollover]
    {144} 71061d 02 01aa03 01aa03  46  0000000000000000 [after 8-bit rollover]
    {145} 70f6a2 00 015402 015401  ae  00...
    {142} 70f6a0 88 015400 015400  24  00...
    {143} 70f6a2 46 00a800 015401  e2  00...
    {144} 70f6a2 48 00aa02 00aa00  3d  00...
    {144} 70f6a2 02 005408 015406  0a  00...
    {141} 70f6a2 04 01540e 01540b  90  00...
    {142} 70f6a2 0a 00aa04 015410  48  00...
    {143} 70f6a2 04 00aa0a 01541b  12  00...
    {142} 70f6a2 0c 00aa0a 01541a  ac  00...
    {144} 70f6a2 04 00aa0d 00aa0d  89  00...
    {143} 70f6a2 0c 00aa0d 00aa0d  56  00...

## LTV-W1 (also LTV-W2):

Full preamble is `aaaaaaaaaaaaaa d2aa2dd4`.

    ID:24h BATTLOW:1b STARTUP:1b ?:2b SEQ:3h ?:1b 8h8h8h WIND:12d 12h CRC:8h TRAILER 8h8h8h8h8h8h8h8h

    d2aa2dd4 0fb220 0e aaaaaa 07f aaa fe 00000000000000 [13 km Good battery]
    d2aa2dd4 0fb220 02 aaaaaa 0bf aaa ad 00000000000000 [19 km Good battery]
    d2aa2dd4 0fb220 08 aaaaaa 011 aaa 39 00000000000000 [4 km Good battery]
    d2aa2dd4 0fb220 0a aaaaaa 000 aaa f2 00000000000000 [2 km Good battery]
    d2aa2dd4 0fb220 06 aaaaaa 000 aaa da 00000000000000 [0 km Good battery]
    d2aa2dd4 0fb220 0e aaaaaa 000 aaa 05 00000000000000 [0 km]
    d2aa2dd4 0fb220 06 aaaaaa 000 aaa da 00000000000000 [0 km]
    d2aa2dd4 0fb220 0e aaaaaa 000 aaa 05 00000000000000 [0 km]
    d2aa2dd4 0fb220 0a aaaaaa 000 aaa f2 00000000000000 [0 km]
    d2aa2dd4 0fb220 42 aaaaaa 000 aaa 73 00000000000000 [startup good]
    d2aa2dd4 0fb220 44 aaaaaa 000 aaa 67 00000000000000 [startup good]
    d2aa2dd4 0fb220 0a aaaaaa 000 aaa f2 00000000000000 [good]
    d2aa2dd4 0fb220 c2 aaaaaa 000 aaa cf 00000000000000 [startup weak]
    d2aa2dd4 0fb220 c4 aaaaaa 000 aaa db 00000000000000 [startup weak]
    d2aa2dd4 0fb220 c6 aaaaaa 000 aaa 38 00000000000000 [startup weak]
    d2aa2dd4 0fb220 c8 aaaaaa 000 aaa f3 00000000000000 [weak]
    d2aa2dd4 0fb220 8a aaaaaa 000 aaa 4e 00000000000000 [weak]
*/

static int lacrosse_r1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble (LTV-R1) is `fff00000 aaaaaaaa d2aa2dd4`
    // full preamble (LTV-R3, LTV-W1) is `aaaaaaaaaaaaaa d2aa2dd4`
    uint8_t const preamble_pattern[] = {0xd2, 0xaa, 0x2d, 0xd4};

    uint8_t b[20];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    int msg_len = bitbuffer->bits_per_row[0];
    if (msg_len < 200) { // allows shorter preamble for LTV-R3
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    } else if (msg_len > 272) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    } else {
        decoder_logf(decoder, 1, __func__, "packet length: %d", msg_len);
    }

    int offset = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= msg_len) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 20 * 8);

    int rev = 1;
    int chk = crc8(b, 11, 0x31, 0x00);
    if (chk == 0
            && b[4] == 0xaa && b[5] == 0xaa && b[6] == 0xaa
            && (b[8] & 0x0f) == 0x0a && b[9] == 0xaa) {
        rev = 9; // LTV-W1/W2
    }
    else if (chk == 0 && b[10] != 0) {
        rev = 3; // LTV-R3
    }
    else {
        chk = crc8(b, 8, 0x31, 0x00);
        if (b[10] != 0 || chk != 0) { // make sure this really is a LTV-R1 and not just a CRC collision
            decoder_log(decoder, 1, __func__, "CRC failed!");
            return DECODE_FAIL_MIC;
        }
    }

    decoder_log_bitrow(decoder, 1, __func__, b, bitbuffer->bits_per_row[0] - offset, "");

    // Note that the rain zero value is 00aa00 with a known byte order of HH??LL.
    // We just prepend the middle byte and assume whitening. Let's hope we get feedback someday.
    int id        = (b[0] << 16) | (b[1] << 8) | b[2];
    int flags     = (b[3] & 0x31); // masks off knonw bits
    int batt_low  = (b[3] & 0x80) >> 7;
    int startup   = (b[3] & 0x40) >> 6;
    int seq       = (b[3] & 0x0e) >> 1;
    int raw_rain1 = ((b[5] ^ 0xaa) << 16) | (b[4] << 8) | (b[6]);
    int raw_rain2 = ((b[8] ^ 0xaa) << 16) | (b[7] << 8) | (b[9]); // only LTV-R3
    int raw_wind  = (b[7] << 4) | (b[8] >> 4); // only LTV-W1/W2

    // Seems rain is 0.25mm per tip, not sure what rain2 is
    float rain_mm = raw_rain1 * 0.25f;
    float rain2_mm = raw_rain2 * 0.25f;
    // wind speed on LTV-W1/W2
    float wspeed_kmh = raw_wind * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_COND,   rev == 1,  DATA_STRING, "LaCrosse-R1",
            "model",            "",                 DATA_COND,   rev == 3,  DATA_STRING, "LaCrosse-R3",
            "model",            "",                 DATA_COND,   rev == 9,  DATA_STRING, "LaCrosse-W1",
            "id",               "Sensor ID",        DATA_FORMAT, "%06x",    DATA_INT,    id,
            "battery_ok",       "Battery level",    DATA_INT,    !batt_low,
            "startup",          "Startup",          DATA_COND,   startup,   DATA_INT,    startup,
            "seq",              "Sequence",         DATA_INT,    seq,
            "flags",            "Unknown",          DATA_COND,   flags,     DATA_INT,    flags,
            "rain_mm",          "Total Rain",       DATA_COND,   rev != 9,  DATA_FORMAT, "%.2f mm", DATA_DOUBLE, rain_mm,
            "rain2_mm",         "Total Rain2",      DATA_COND,   rev == 3,  DATA_FORMAT, "%.2f mm", DATA_DOUBLE, rain2_mm,
            "wind_avg_km_h",    "Wind Speed",       DATA_COND,   rev == 9,  DATA_FORMAT, "%.1f km/h", DATA_DOUBLE, wspeed_kmh,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "startup",
        "seq",
        "flags",
        "rain_mm",
        "rain2_mm",
        "wind_avg_km_h",
        "mic",
        NULL,
};

// flex decoder m=FSK_PCM, s=104, l=104, r=9600
r_device const lacrosse_r1 = {
        .name        = "LaCrosse Technology View LTV-R1, LTV-R3 Rainfall Gauge, LTV-W1/W2 Wind Sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 104,
        .long_width  = 104,
        .reset_limit = 9600,
        .decode_fn   = &lacrosse_r1_decode,
        .fields      = output_fields,
};
