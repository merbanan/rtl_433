/** @file
    LaCrosse Technology View LTV-WSDTH01 Breeze Pro Wind Sensor.

    Copyright (C) 2020 Mike Bruski (AJ9X) <michael.bruski@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
LaCrosse Technology View LTV-WSDTH01 Breeze Pro Wind Sensor.

LaCrosse Color Forecast Station (model 79400) utilizes the remote temp/
humidity/wind speed/wind direction sensor LTV-WSDTH01.

Product pages:
https://www.lacrossetechnology.com/products/79400
https://www.lacrossetechnology.com/products/ltv-wsdth01

Specifications:
- Wind Speed Range: 0 to 178 kmh
- Degrees of Direction: 360 deg with 16 Cardinal Directions
- Outdoor Temperature Range: -29 C to 60 C
- Outdoor Humidity Range: 10 to 99 %RH
- Update Interval: Every 31 Seconds

Internal inspection of the remote sensor reveals that the device
utilizes a HopeRF CMT2119A ISM transmitter chip which is capable of
transmitting up to 32 bytes of data on any ISM frequency using OOK
or (G)FSK modulation.  In this application, the sensor sends
FSK_PCM on a center frequency of 914.938 MHz.  FWIW, FCC filings
and photos would seem to indicate that the LTV-WSDTH01 and TX145wsdth
are physically identical devices with different antenna.  The MCU
programming of the latter is most likely different given it transmits
an OOK data stream on 432.92 MHz.

An inspection of the 79400 console reveals that it employs a HopeRF
CMT2219A ISM receiver chip.  An application note is available that
provides further info into the capabilities of the CMT2119A and
CMT2219A.

(http://www.cmostek.com/download/CMT2119A_v0.95.pdf)
(http://www.cmostek.com/download/CMT2219A.pdf)
(http://www.cmostek.com/download/AN138%20CMT2219A%20Configuration%20Guideline.pdf)

Protocol Specification:

Data bits are NRZ encoded with logical 1 and 0 bits 106.842us in length.

    SYNC:32h ID:24h ?:4b SEQ:3d ?:1b TEMP:12d HUM:12d WSPD:12d WDIR:12d CHK:8h END:32h

Packet length is 264 bits according to inspectrum broken down as follows:

- warm-up:         7 bytes (0x55, aligned with sync word these are 0xaa)
- preamble/sync    4 bytes 0xd2aa2dd4 (see as 0x695516ea05)
- device id:       3 bytes (matches bar code underside of unit covering pgm port)
- x1:              4 bit   (unknown, bit 0?00 might be 'battery low')
- sequence:        3 bits  (0-7, one up per packet, then repeats)
- x2:              1 bit   (unknown)
- celsius:        12 bits  (offset 400, scale 10, range: -29 C to 60 C)
- humidity:       12 bits  (10 to 99% relative humidity)
- wind speed:     12 bits  (0.0 to 178.0 kMh)
- wind direction: 12 bits  (0 to 359 deg)
- checksum:        8 bits  (CRC-8 poly 0x31 init 0x00 over 10 bytes after sync)
- trailer:        32 bytes (0xd2d2d200)

The sensor generates a packet every 'n' seconds but only transmits if one or
more of the following conditions are satisfied:

- temp changes +/- 0.8 degrees C
- humidity changes +/- 1%
- wind speed changes +/- 0.5 kM/h

Thus, if there is a gap in sequencing, it is due to bad packet[s] (too short,
failed CRC) or packet[s] that didn't satisfy at least one of these three
conditions. 'n' above varies with temperature.  At 0C and above, 'n' is 31.
Between -17C and 0C, 'n' is 60.  Below -17C, 'n' is 360.

*/

#include "decoder.h"

static int lacrosse_breezepro_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xd2, 0xaa, 0x2d, 0xd4};

    data_t *data;
    uint8_t b[11];
    uint32_t id;
    int flags, seq, offset, chk;
    int raw_temp, humidity, raw_speed, direction;
    float temp_c, speed_kmh;

    if (bitbuffer->bits_per_row[0] < 264) {
        decoder_logf(decoder, 1, __func__, "Wrong packet length: %d", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH;
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

    decoder_log_bitbuffer(decoder, 1, __func__, bitbuffer, "");

    id        = (b[0] << 16) | (b[1] << 8) | b[2];
    flags     = (b[3] & 0xf1); // masks off seq bits
    seq       = (b[3] & 0x0e) >> 1;
    raw_temp  = b[4] << 4 | ((b[5] & 0xf0) >> 4);
    humidity  = ((b[5] & 0x0f) << 8) | b[6];
    raw_speed = b[7] << 4 | ((b[8] & 0xf0) >> 4);
    direction = ((b[8] & 0x0f) << 8) | b[9];

    // base and/or scale adjustments
    temp_c = (raw_temp - 400) * 0.1f;
    speed_kmh = raw_speed * 0.1f;

    if (humidity < 0 || humidity > 100
        || temp_c < -40 || temp_c > 70
        || direction < 0 || direction > 360
        || speed_kmh < 0 || speed_kmh > 200)
      return DECODE_FAIL_SANITY;

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "LaCrosse-BreezePro",
            "id",               "Sensor ID",        DATA_FORMAT, "%06x", DATA_INT, id,
            "seq",              "Sequence",         DATA_FORMAT, "%01x", DATA_INT, seq,
            "flags",            "unknown",          DATA_INT,     flags,
            "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",         DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "wind_avg_km_h",    "Wind speed",       DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, speed_kmh,
            "wind_dir_deg",     "Wind direction",   DATA_INT,    direction,
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
        "temperature_C",
        "humidity",
        "wind_avg_km_h",
        "wind_dir_deg",
        "mic",
        NULL,
};

// flex decoder m=FSK_PCM, s=107, l=107, r=5900
r_device const lacrosse_breezepro = {
        .name        = "LaCrosse Technology View LTV-WSDTH01 Breeze Pro Wind Sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 107,
        .long_width  = 107,
        .reset_limit = 5900,
        .decode_fn   = &lacrosse_breezepro_decode,
        .fields      = output_fields,
};
