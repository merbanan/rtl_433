/** @file
    LaCrosse Breeze Pro LTV-WSDTH01 sensor.

    Copyright (C) 2020 Mike Bruski (AJ9X) <michael.bruski@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
LaCrosse Breeze Pro LTV-WSDTH01 sensor.

LaCrosse Color Forecast Station (model 79400) utilizes the remote temp/
humidity/wind speed/wind direction sensor LTV-WSDTH01.

Product pages:
https://www.lacrossetechnology.com/products/79400
https://www.lacrossetechnology.com/products/ltv-wsdth01

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
(http://www.cmostek.com/download/AN138%20CMT2219A%20Configuration%20Guideline.pdf)

Protocol Specification:
 
Data bits are NRZ encoded with logical 1 and 0 bits 106.841us in length.

Packet length is 266 bits according to inspectrum broken down as follows:

sync:		 7 bytes (0x55)
preamble         5 bytes (0x695516ea05)
device ID:      19 bits  (no change after replacing rechargeable cell)
x1:              1 bit   (unknown)
x2:              1 bit   (unknown)
sequence:        3 bits  (0-7, one up per packet, rinse, lather, repeat)
x3:              1 bit   (unknown)
celsius:        12 bits  (base 400, scale 10, range: -29°C to 60°C)
humidity:       12 bits  (10 to 99% relative humidity)
wind speed:     12 bits  (0.0 to 178.0 kMh)
wind direction: 12 bits  (0 to 359°)
CRC:             8 bits  (haven't discovered details yet)
postamble:       3 bytes (0x696969)
reset:          56 bits  (all zeros)

Given one or more of the following conditions is satisfied since the time of
the last packet transmission:

	temp      changes +/- 0.5°C 
	humidity  changes +/- 2%
	speed     changes +/- 0.8kM

an update packet will be sent.  To conserve battery power, the interval between
packets is adjusted as temperature decreases.  At temperatures above 0°C, the
interval between packets will be 31 seconds (assuming the above conditions are
satisfied).  Between 0°C and -17°C, the packet interval is 1 minute.  Below 17°C,
the time between packets increases to 6 minutes.

I tried using CRC check for TX145wsdth but that failed so I'm not checking it
for the time being.  If someone wants to crack it, please be my guest.

- checksum CRC-8 poly 0x31 init 0x00 over preceeding 7 bytes - tried but failed

*/

#include "decoder.h"

static const uint8_t preamble_pattern[] = { 0x69, 0x55, 0x16, 0xea, 0x05 };

static int lacrosse_breezepro_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t p[32], l, *c, x1, x2, x3, seq, offset, chk, mic;
    uint32_t id;
    uint16_t raw_temp, humidity, raw_speed, direction;
    float temp_c, speed_kmh;

    if (decoder->verbose > 1) {
          bitrow_debug(0, bitbuffer->bits_per_row[0]);
    }

    if (bitbuffer->bits_per_row[0] != 264) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: bits_per_row: %d\n", __func__, bitbuffer->bits_per_row[0]);
        }
        return DECODE_ABORT_LENGTH;
    }

    offset = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof (preamble_pattern) * 8);

    if (offset == bitbuffer->bits_per_row[0]) {
        return DECODE_FAIL_SANITY;
    }


//    c = bitbuffer->bb[0];

//    chk = crc8(c, 8, 0x31, 0x00);
//    if (chk) {
//        if (decoder->verbose > 1) {
//            fprintf(stderr, "%s: CRC failed!\n", __func__);
//        }
//        return DECODE_FAIL_MIC;
//    }

    /* align row - not the best align but I can work with it */
    l = bitbuffer->bits_per_row[0] - offset;
    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, p, l);

    id          = ((p[2] & 0xe0) >> 5) | (p[1] << 3) | (p[0] << 11);
    x1          = (p[2] & 0x10) >> 4;
    x2          = (p[2] & 0x08) >> 3;
    seq         = p[2] & 0x07;
    x3          = (p[3] & 0x80) >> 7;
    raw_temp    = ((p[4] & 0xf8) >> 3) | (p[3] & 0x7f) << 5;
    humidity    = ((p[6] & 0x80) >> 7) | (p[5] << 1) | ((p[4] & 0x07) << 9);
    raw_speed   = ((p[7] & 0xf8) >> 3) | (p[6] & 0x7f) << 5;
    direction   = ((p[9] & 0x80) >> 7) | (p[8] << 1) | ((p[7] & 0x07) << 9);
    mic         = ((p[10] & 0x80) >> 7) | (p[9] & 0x7f) << 1;    
    /* base and/or scale adjustments */
    temp_c = (float)raw_temp * 0.1 - 40.0;
    speed_kmh = (float)raw_speed * 0.1;

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "LaCrosse-LTV-WSDTH01",
            "id",               "Sensor ID",        DATA_FORMAT, "%05x", DATA_INT, id,
	    "x1",		"unknown",          DATA_INT,    x1,
	    "x2",		"unknown",	    DATA_INT,    x2,
            "seq",              "Sequence",         DATA_FORMAT, "%01x", DATA_INT, seq,
            "x3",               "unknown",          DATA_INT,    x3,
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

static char *output_fields[] = {
        "model",
        "id",
	"x1",
	"x2"
	"seq",
        "x3",
        "temperature_C",
        "humidity",
        "wind_avg_km_h",
        "wind_dir_deg",
        "test",
        "mic",
        NULL,
};

// flex decoder m=FSK_PCM, s=107, l=107, r=5900
r_device lacrosse_breezepro = {
        .name        = "LaCrosse LTV-WSDTH01 sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 107,
        .long_width  = 107,
        .reset_limit = 5900,
        .decode_fn   = &lacrosse_breezepro_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
