/** @file
    LaCrosse Technology View LTV-WR1 Multi Sensor.

    Copyright (C) 2020 Mike Bruski (AJ9X) <michael.bruski@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
LaCrosse Technology View LTV-WR1 Multi Sensor.

LaCrosse Color Forecast Station (model S84060?) utilizes the remote
Thermo/Hygro LTV-TH3 and LTV-WR1 multi sensor (wind spd/dir and rain).

Product pages:
https://www.lacrossetechnology.com/products/S84060
https://www.lacrossetechnology.com/products/ltv-wr1

Specifications:
- Wind Speed Range: 0 to 188 kmh
- Degrees of Direction: 0 to 359 degrees
- Rainfall 0 to 9999.9 mm
- Update Interval: Every 30 Seconds

No internal inspection of the sensors was performed so can only
speculate that the remote sensors utilize a HopeRF CMT2119A ISM
transmitter chip which is tuned to 915Mhz.

Again, no inspection of the S84060 console was performed but it
probably employs a HopeRF CMT2219A ISM receiver chip.  An
application note is available that provides further info into the
capabilities of the CMT2119A and CMT2219A.

(http://www.cmostek.com/download/CMT2119A_v0.95.pdf)
(http://www.cmostek.com/download/CMT2219A.pdf)
(http://www.cmostek.com/download/AN138%20CMT2219A%20Configuration%20Guideline.pdf)

Protocol Specification:

Data bits are NRZ encoded with logical 1 and 0 bits 104us in length.

LTV-WR1
    SYN:32h ID:24h ?:4b SEQ:3d ?:1b WSPD:12d WDIR:12d RAIN1:12d RAIN2:12d CHK:8h

    CHK is CRC-8 poly 0x31 init 0x00 over 10 bytes following SYN

*/

#include "decoder.h"

static int lacrosse_wr1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xd2, 0xaa, 0x2d, 0xd4};

    data_t *data;
    uint8_t b[11];
    uint32_t id;
    int flags, seq, offset, chk;
    int raw_wind, direction, raw_rain1, raw_rain2;
    float speed_kmh;
    // float rain_mm;

    if (bitbuffer->bits_per_row[0] < 120) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH;
    } else if (bitbuffer->bits_per_row[0] > 156) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH;
    } else {
        decoder_logf(decoder, 1, __func__, "packet length: %d", bitbuffer->bits_per_row[0]);
    }

    offset = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 11 * 8);

    chk = crc8(b, 11, 0x31, 0x00);
    if (chk) {
        decoder_log(decoder, 1, __func__, "CRC failed!");
        return DECODE_FAIL_MIC;
    }

    id        = (b[0] << 16) | (b[1] << 8) | b[2];
    flags     = (b[3] & 0xf1); // masks off seq bits
    seq       = (b[3] & 0x0e) >> 1;
    raw_wind  = b[4] << 4 | ((b[5] & 0xf0) >> 4);
    direction = ((b[5] & 0x0f) << 8) | b[6];
    raw_rain1 = b[7] << 4 | ((b[8] & 0xf0) >> 4);
    raw_rain2 = ((b[8] & 0x0f) << 8) | b[9];

    // base and/or scale adjustments
    speed_kmh = raw_wind * 0.1f;
    if (speed_kmh < 0 || speed_kmh > 200 || direction < 0 || direction > 360)
        return DECODE_FAIL_SANITY;

    //rain_mm   = 0.0;  // dummy until we know what raw_rain1 and raw_rain2 mean

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "LaCrosse-WR1",
            "id",               "Sensor ID",        DATA_FORMAT, "%06x", DATA_INT, id,
            "seq",              "Sequence",         DATA_INT,     seq,
            "flags",            "unknown",          DATA_INT,     flags,
            "wind_avg_km_h",        "Wind speed",       DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, speed_kmh,
            "wind_dir_deg",     "Wind direction",   DATA_INT,    direction,
            "rain1",            "raw_rain1",        DATA_FORMAT, "%03x", DATA_INT, raw_rain1,
            "rain2",            "raw_rain2",        DATA_FORMAT, "%03x", DATA_INT, raw_rain2,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "seq",
        "flags",
        "wind_avg_km_h",
        "wind_dir_deg",
        "rain1",
        "rain2",
        "mic",
        NULL,
};

// flex decoder m=FSK_PCM, s=104, l=104, r=9600
r_device const lacrosse_wr1 = {
        .name        = "LaCrosse Technology View LTV-WR1 Multi Sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 104,
        .long_width  = 104,
        .reset_limit = 9600,
        .decode_fn   = &lacrosse_wr1_decode,
        .fields      = output_fields,
};
