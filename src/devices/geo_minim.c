/** @file
    GEO mimim+ energy monitor.

    Copyright (C) 2022 Lawrence Rust, lvr at softsystem dot co dot uk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/** @fn int minim_decode(r_device *decoder, bitbuffer_t *bitbuffer)
GEO mimim+ energy monitor.

The GEO minim+ energy monitor comprises a sensor unit and a display unit.
https://assets.geotogether.com/sites/4/20170719152420/Minim-Data-sheet.pdf

The sensor unit is supplied with a detachable current transformer that is
clipped around the live wire feeding the monitored device. The sensor unit
is powered by 3x AA batteries that provide for ~2 years of operation. It
transmits a short (5mS) data packet every ~3 seconds.

Frequency 868.29 MHz, bit period 25 microseconds (40kbps), modulation FSK_PCM

The display unit requires a 5V supply, provided by the supplied mains/USB
adapter. The display and sensor units are paired during initial power on
or as follows:

1. On the display, hold down the <- and +> buttons together for 3 seconds.
2. At the next screen, hold down the middle button for 3 seconds until the
   display shows “Pair?”
3. On the sensor, press and hold the pair button (next to the red light)
   until the red LED light illuminates.
4. Release the pair button and the LED flashes as the transmitter pairs.
5. The display should now read “Paired CT"

When paired the display listens for sensor packets and then transmits a
summary packet using the same protocol.

The following Flex decoder will capture the raw data:

    rtl_433 -f 868.29M -s 1024k -Y classic -X 'n=minim+,m=FSK_PCM,s=24,l=24,r=3000,preamble=0x7bb9'
*/

#include <time.h>

#include "decoder.h"

/**
GEO minim+ current sensor.

Packet layout:

- 24 bit preamble of alternating 0s and 1s
- 2 sync bytes: 0x7b 0xb9
- 4 byte header: 0x3f 0x06 0x29 0x05
- 5 data bytes
- CRC16

The following Flex decoder will capture the raw sensor data:

    rtl_433 -f 868.29M -s 1024k -Y classic -X 'n=minim+ sensor,m=FSK_PCM,s=24,l=24,r=3000,preamble=0x7bb93f'

Data format string:

    ID:24h VA:13d 3x UP:24d CRC:16h

    VA: Big endian power x10VA, bit14 = 5VA
    UP: Big endian uptime x9 seconds
*/
static int geo_minim_ct_sensor_decode(r_device * const decoder, uint8_t const buf[], unsigned len)
{
    unsigned n, va, flags4;

    if (len != 11) {
        decoder_logf_bitrow(decoder, 1, __func__, buf, 8 * len,
            "Incorrect length. Expected 11 got %u bytes", len);
        return DECODE_ABORT_LENGTH;
    }

    char id[9];
    snprintf(id, sizeof(id), "%02X%02X%02X%02X", buf[0], buf[1], buf[2], buf[3]);

    // Uptime in ~8 second intervals
    unsigned n = 8 * (buf[8] + (buf[7] << 8) + (buf[6] << 16));

    // Convert to days,hours,minutes,seconds
    unsigned secs = n % 60;
    n /= 60;
    unsigned mins = n % 60;
    n /= 60;
    unsigned hours = n % 24;
    n /= 24;
    char up[32];
    snprintf(up, sizeof(up), "%uday %02u:%02u:%02u", n, hours, mins, secs);

    // Bytes 4 & 5 appear to be the instantaneous VA x10.
    // When scaled by the 'Fine Tune' setting (power factor [0.88]) set on the
    // display unit it matches the Watts value in display messages.
    unsigned va = 10 * (buf[5] + ((buf[4] & 0x0f) << 8));
    if (buf[4] & 0x40)
        va += 5;

    // TODO: what are the flag bits in buf[4] (0x30)? Battery OK, Fault?
    unsigned flags4 = buf[4] & ~0x4f;

    /* clang-format off */
    data_t *data = data_make(
            "model",    "Device",       DATA_STRING, "GEO-minimCT",
            "id",       "ID",           DATA_STRING, id,
            "va",       "VA",           DATA_INT, va,
            "flags4",   "Flags",        DATA_COND, flags4 != 0x30, DATA_FORMAT, "%#x", DATA_INT, flags4,
            "uptime",   "Uptime",       DATA_STRING, up,
            "mic",      "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1; // Message successfully decoded
}


/**
GEO minim+ display.

Packet layout:

- 24 bit preamble of alternating 0s and 1s
- 2 sync bytes: 0x7b 0xb9
- 4 byte header: 0xea 0x01 0x35 0x2a
- 42 data bytes
- CRC16

The following Flex decoder will capture the raw display data:
rtl_433 -f 868.29M -s1024000 -Y classic\
        -X'n=minim+ display,m=FSK_PCM,s=24,l=24,r=3000,preamble=0x7bb9ea'

Data format string:

    ID:24h PWR:15d 1x 64x WH:11d 5x 64x 48x MIN:8d HRS:8d DAYS:16d 96x CRC:16h

    PWR: Instantaneous power, little endian
    WH: Watt-hours in last 15 minutes, little endian
    MIN,HRS,DAYs since 1/1/2007, little endian
*/
static int geo_minim_display_decode(r_device * const decoder, uint8_t const buf[], unsigned len)
{
    uint8_t const zeroes[8] = { 0 };
    uint8_t const aaes[5] = { 0xaa, 0xaa, 0xaa, 0xaa, 0xaa };
    uint8_t const trailer[12] = { 0xaa, 0xff, 0xff, 0, 0, 0, 0, 0xaa, 0xff, 0xaa, 0xaa, 0 };

    if (len != 48) {
        decoder_logf_bitrow(decoder, 1, __func__, buf, 8 * len,
                "Incorrect length. Expected 48, got %u bytes", len);
        return DECODE_ABORT_LENGTH;
    }

    // Report unexpected values
    if (memcmp(zeroes, buf + 6, sizeof(zeroes))) {
        decoder_logf_bitrow(decoder, 1, __func__, buf + 6, 8 * sizeof(zeroes),
                "Nonzero @6");
        //return DECODE_FAIL_SANITY;
    }

    if (memcmp(zeroes, buf + 16, sizeof(zeroes))) {
        decoder_logf_bitrow(decoder, 1, __func__, buf + 16, 8 * sizeof(zeroes),
                "Nonzero @16");
        //return DECODE_FAIL_SANITY;
    }

    if (memcmp(aaes, buf + 24, sizeof(aaes))) {
        decoder_logf_bitrow(decoder, 1, __func__, buf + 24, 8 * sizeof(aaes),
                "Not 0xaa @24");
        //return DECODE_FAIL_SANITY;
    }

    if (buf[29] != 0x00) {
        decoder_logf(decoder, 1, __func__,
                "Expected 0x00 but got %#x @29", buf[29]);
        //return DECODE_FAIL_SANITY;
    }

    if (memcmp(trailer, buf + 34, sizeof(trailer))) {
        decoder_logf_bitrow(decoder, 1, __func__, buf + 34, 8 * sizeof(trailer),
                "Bad trailer @34");
        //return DECODE_FAIL_SANITY;
    }

    char id[9];
    snprintf(id, sizeof(id), "%02X%02X%02X%02X", buf[0], buf[1], buf[2], buf[3]);

    // Instantaneous power: 300W => 60: 1 = 5W
    unsigned watts = 5 * (buf[4] + ((buf[5] & 0x7f) << 8));
    // TODO: what is bit7?
    unsigned flags5 = buf[5] & ~0x7f;

    // Energy: 480W => 8/min: 1 = 0.06kWm = 0.001kWh
    unsigned wh = buf[14] + ((buf[15] & 0x7) << 8);
    // TODO: what are bits 3..7 ? 0x40 normally, Battery OK, Fault?
    unsigned flags15 = buf[15] & ~0x7;

    struct tm t = {0};
    // Date/time @30..33
    t.tm_sec = 0;
    t.tm_min = buf[33] & 0x3f;
    t.tm_hour = buf[32] & 0x1f;
    // Day 0 = 1/1/2007
    t.tm_mday = 1 + buf[30] + (buf[31] << 8);
    t.tm_mon = 1 - 1;
    t.tm_year = 2007 - 1900;
    t.tm_isdst = -1;
    // Normalise the date
    mktime(&t);
    char now[64];
    snprintf(now, sizeof(now), "%04d-%02d-%02d %02d:%02d",
            1900 + t.tm_year, 1 + t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min);

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",             DATA_STRING, "GEO-minimDP",
            "id",       "",             DATA_STRING, id,
            "watts",    "Watts",        DATA_FORMAT, "%u", DATA_INT, watts,
            "kwh",      "kWh",          DATA_FORMAT, "%.3f", DATA_DOUBLE, wh * 0.001,
            "time",     "Time",         DATA_STRING, now,
            "flags5",   "Flags5",       DATA_COND, flags5 != 0,
                                                DATA_FORMAT, "%#x", DATA_INT, flags5,
            "flags15",  "Flags15",      DATA_COND, flags15 != 0x40,
                                                DATA_FORMAT, "%#x", DATA_INT, flags15,
            "mic",      "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1; // Message successfully decoded
}

static int decode_minim_message(r_device *const decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t buf[128];
    unsigned bits = bitbuffer->bits_per_row[ row];

    // Extract frame header
    unsigned const hdr_len = 4;
    unsigned const hdr_bits = hdr_len * 8;
    if (bitpos + hdr_bits >= bits)
        return DECODE_ABORT_LENGTH;

    bits -= bitpos;
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, buf, hdr_bits);

    // Determine frame type. Assume:
    // buf[0] = message type
    // buf[1], buf[2] = session ID from pairing
    // buf[3] = data byte length
    uint8_t const header1[] = { 0xea, /* 0x01, 0x35, 0x2a */ }; // Display
    uint8_t const header2[] = { 0x3f, /* 0x06, 0x29, 0x05 */ }; // CT sensor
    enum EType { kTypeDisplay, kTypeCT } type;
    if (!memcmp(header1, buf, sizeof(header1))) {
        type = kTypeDisplay;
    }
    else if (!memcmp(header2, buf, sizeof(header2))) {
        type = kTypeCT;
    }
    else
    {
        decoder_logf(decoder, 1, __func__,
                "Unknown header %02x%02x%02x%02x",
                buf[0], buf[1], buf[2], buf[3]);
        return DECODE_ABORT_EARLY;
    }

    unsigned bytes = bits / 8;
    unsigned maxlen = sizeof(buf);
    if (bytes > maxlen) {
        decoder_logf(decoder, 1, __func__,
                "Too big: %u > %u max bytes", bits / 8, maxlen);
        //return DECODE_ABORT_LENGTH;
        bytes = maxlen;
    }

    // Check offset to crc16 using data_len @ header[3]
    unsigned crc_len = hdr_len + buf[3];
    if (crc_len + 2 > bytes) {
        decoder_logf(decoder, 1, __func__,
                "Truncated - got %u of %u bytes", bytes, crc_len +2);
        return DECODE_FAIL_SANITY;
    }

    // Extract byte-aligned data
    bitbuffer_extract_bytes(
            bitbuffer, row, bitpos + hdr_bits, buf + hdr_len, (bytes - hdr_len) * 8);

    // Message Integrity Check
    unsigned crc = crc16(buf, crc_len, 0x8005, 0);
    unsigned crc_rcvd = (buf[ crc_len] << 8) | buf[ crc_len +1];
    if (crc != crc_rcvd) {
        decoder_logf_bitrow(decoder, 1, __func__, buf, (crc_len + 2) * 8,
                "Bad CRC. Expected %04X got %04X", crc, crc_rcvd);
        return DECODE_FAIL_MIC;
    }

    switch (type) {
    case kTypeDisplay:
        return geo_minim_display_decode(decoder, buf, bytes);

    case kTypeCT:
        return geo_minim_ct_sensor_decode(decoder, buf, bytes);
    }

    return DECODE_FAIL_SANITY;
}

// List of fields to appear in the `-F csv` output.
static char *output_fields[] = {
        "model",
        "id",
        "va",
        "flags4",
        "uptime",
        "watts",
        "kwh",
        "time",
        "flags5",
        "flags15",
        "mic",
        NULL,
};

static int minim_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const pre[] = { 0x55, 0x55 };
    const unsigned prelen = 8 * sizeof(pre);
    uint8_t const syn[] = { 0x7b, 0xb9 };
    const unsigned synlen = 8 * sizeof(syn);
    int n;

    if (bitbuffer->num_rows != 1)
        return DECODE_ABORT_LENGTH;

    if (bitbuffer->bits_per_row[0] <= prelen)
        return DECODE_ABORT_LENGTH;

    if ((n = bitbuffer_search(bitbuffer, 0, 0, pre, prelen)) >= bitbuffer->bits_per_row[0])
        return DECODE_ABORT_EARLY;

    if (bitbuffer->bits_per_row[0] <= n + prelen + synlen)
        return DECODE_ABORT_LENGTH;

    if ((n = bitbuffer_search(bitbuffer, 0, n + prelen, syn, synlen))
            >= bitbuffer->bits_per_row[0]) {
        if (decoder->verbose >= 1)
            decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "No sync");
        return DECODE_ABORT_EARLY;
    }

    if ((n = decode_minim_message(decoder, bitbuffer, 0, n + synlen)) <= 0)
        return n;

    return 1;
}

r_device geo_minim = {
        .name           = "GEO minim+ energy monitor",
        .modulation     = FSK_PULSE_PCM,
        .short_width    = 24,
        .long_width     = 24,
        .reset_limit    = 3000,
        .decode_fn      = &minim_decode,
        .fields         = output_fields,
};
