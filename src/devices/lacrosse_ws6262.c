/** @file
    Model Lacrosse Technology WS6262 with sensor: WSTX62TY

    Copyright (C) 2025 Sebastien MORIO <seb.morio@gmail.com>

    Based on Emax protocol and vevor vevor_7in1

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Model Lacrosse Technology WS6262 with sensor: WSTX62TY

Method for Creating the WS6262 Decoder:

The EMAX and Vevor_7in1 protocols are not fully compatible with the WS6262 (fields not adapted).
Capture raw frames using:
/usr/local/bin/rtl_433 -S unknown -R 0 to retrieve raw HEX data.

Decode the captured frames, e.g.:
/usr/local/bin/rtl_433 -A -r g015_433.92M_250k.cu8

Create the lacrosse_ws6262 decoder based on EMAX and Vevor_7in1.

Lacrosse WS6262 Station Météo Pro 
    Temperature sensor
    Humidity sensor
    Rain sensor
    Wind Direction sensor
    Wind speed sensor and Wind Gust
    UV sensor
    LUX Sensor

Extract Code : from 2 different Weather Station (Both are WS6262 Weather Station id b61 and id 694) 
Raw data:
codes     : {1340}fffc000555555555565652a55055b08012a3180808094816300827180020283038404880889098a0a8b21ff2200007ffff8000aaaaaaaaaacaca54aa0ab6100254630101012902c60104e3000405060708091011121314151643fe440000fffff000155555555559594a954156c2004a8c602020252058c0209c600080a0c0e101220222426282a2c87fc8800000000000000000000000000000000000000000000000000000000
codes     : {1342}fffc000555555555565652a55055b08012ab180808094816300827c80020283038404880889098a0a8b3f283f80007ffff8000aaaaaaaaaacaca54aa0ab6100255630101012902c60104f900040506070809101112131415167e507f0000fffff000155555555559594a954156c2004aac602020252058c0209f200080a0c0e101220222426282a2cfca0fe000000000000000000000000000000000000000000000000000000000
codes     : {1342}fffc000555555555565652a550534a1815b1e0080813180848080a700020283038404880889098a0a8b52b35300007ffff8000aaaaaaaaaacaca54aa0a694302b63c01010263010901014e0004050607080910111213141516a566a60000fffff000155555555559594a95414d286056c78020204c6021202029c00080a0c0e101220222426282a2d4acd4c000000000000000000000000000000000000000000000000000000000
codes     : {1342}fffc000555555555565652a55055b08012ab180808094816300828d80020283038404880889098a0a8b7a74fa80007ffff8000aaaaaaaaaacaca54aa0ab6100255630101012902c601051b0004050607080910111213141516f4e9f50000fffff000155555555559594a954156c2004aac602020252058c020a3600080a0c0e101220222426282a2de9d3ea000000000000000000000000000000000000000000000000000000000
codes     : {1341}fffc000555555555565652a550534a1815c1e0080813180848080a700020283038404880889098a0a8b0def0e00007ffff8000aaaaaaaaaacaca54aa0a694302b83c01010263010901014e00040506070809101112131415161bde1c0000fffff000155555555559594a95414d286057078020204c6021202029c00080a0c0e101220222426282a2c37bc38000000000000000000000000000000000000000000000000000000000
codes     : {1342}fffc000555555555565652a55055b08012ab180808094816300829680020283038404880889098a0a8b3538b580007ffff8000aaaaaaaaaacaca54aa0ab6100255630101012902c601052d00040506070809101112131415166a716b0000fffff000155555555559594a954156c2004aac602020252058c020a5a00080a0c0e101220222426282a2cd4e2d6000000000000000000000000000000000000000000000000000000000

- Preamble 
    ff ff 80 00 aa aa aa aa aa ca ca 54
	
Byte Position   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32
               AA KC II IB 0T TT HH 0W WW 0D DD RR RR UU LL LL GG 05 06 07 08 09 10 11 12 13 14 15 16 17 xx SS yy
               aa 0a b6 10 02 54 63 01 01 01 29 02 c6 01 04 e3 00 04 05 06 07 08 09 10 11 12 13 14 15 16 43 fe 44
               aa 0a b6 10 02 55 63 01 01 01 29 02 c6 01 04 f9 00 04 05 06 07 08 09 10 11 12 13 14 15 16 7e 50 7f
               aa 0a 69 43 02 b6 3c 01 01 02 63 01 09 01 01 4e 00 04 05 06 07 08 09 10 11 12 13 14 15 16 a5 66 a6
               aa 0a b6 10 02 55 63 01 01 01 29 02 c6 01 05 1b 00 04 05 06 07 08 09 10 11 12 13 14 15 16 f4 e9 f5
               aa 0a 69 43 02 b8 3c 01 01 02 63 01 09 01 01 4e 00 04 05 06 07 08 09 10 11 12 13 14 15 16 1b de 1c
               aa 0a b6 10 02 55 63 01 01 01 29 02 c6 01 05 2d 00 04 05 06 07 08 09 10 11 12 13 14 15 16 6a 71 6b


- K: (4 bit) Kind of device, = A if Temp/Hum Sensor or = 0 if Weather Rain/Wind station
- C: (4 bit) channel ( = 4 for Weather Rain/wind station)
- I: (12 bit) ID
- B: (4 bit) BP01: battery low, pairing button, 0, 1
- T: (12 bit) temperature in C, offset 500, scale 10
- H: (8 bit) humidity %
- R: (16) Rain
- W: (12) Wind speed
- D: (9 bit) Wind Direction
- U: (5 bit) UV index
- L: (1 + 15 bit) Lux value, if first bit = 1 , then x 10 the rest.
- G: (8 bit) Wind Gust
- A: (4 bit) fixed values of 0xA
- 0: (4 bit) fixed values of 0x0
- x: (8 bit) incremental value each tx
- S: (8 bit) checksum
- y: (8 bit) incremental value each tx yy = xx + 1

line added in : 
          ./include/rtl_433_devices.h
              DECL(lacrosse_ws6262)
		  
          ./src/CMakeLists.txt
			  devices/lacrosse_ws6262.c
*/

#define LACROSSE_WSTX_BITLEN     264   //33 * 8

static int lacrosse_WS6262_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is ffffaaaaaaaaaacaca54
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0xca, 0xca, 0x54};

    // Because of a gap false positive if LUX at max for weather station, only single row to be analyzed with expected 3 repeats inside the data.
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int ret = 0;
    int pos = 0;
    while ((pos = bitbuffer_search(bitbuffer, 0, pos, preamble_pattern, sizeof(preamble_pattern) * 8)) + LACROSSE_WSTX_BITLEN <= bitbuffer->bits_per_row[0]) {
        if (pos >= bitbuffer->bits_per_row[0]) {
            decoder_log(decoder, 2, __func__, "Preamble not found");
            ret = DECODE_ABORT_EARLY;
            continue;
        }
        decoder_logf(decoder, 2, __func__, "Found Emax preamble pos: %d", pos);

        pos += sizeof(preamble_pattern) * 8;
        // we expect at least 32 bytes
        if (pos + 32 * 8 > bitbuffer->bits_per_row[0]) {
            decoder_log(decoder, 2, __func__, "Length check fail");
            ret = DECODE_ABORT_LENGTH;
            continue;
        }
        uint8_t b[32] = {0};
        bitbuffer_extract_bytes(bitbuffer, 0, pos, b, sizeof(b) * 8);

        // verify checksum
        if ((add_bytes(b, 31) & 0xff) != b[31]) {
            decoder_log(decoder, 2, __func__, "Checksum fail");
            ret = DECODE_FAIL_MIC;
            continue;
        }

        int id          = (b[2] << 4) | (b[3] >> 4);
		//printf("ID détecté: 0x%X (%d)\n", id, id);

        int channel     = (b[1] & 0x0f);
        int battery_low = (b[3] & 0x08);
        int pairing     = (b[3] & 0x04);

		if (b[0] == 0xAA && b[1] == 0x0a) {
            int temp_raw      = ((b[4] & 0x0f) << 8) | (b[5]);
            float temp_c      = (temp_raw - 500) * 0.1f;
		    
            int humidity      = b[6];
		    
            int wind_raw      = (((b[7] - 1) & 0xff) << 8) | ((b[8] - 1) & 0xff);   // need to remove 1 from byte , 0x01 - 1 = 0 , 0x02 - 1 = 1 ... 0xff -1 = 254 , 0x00 - 1 = 255.
            float speed_kmh   = wind_raw * 0.2f;
            float gust_kmh = b[16] / 1.5f;
            int direction_deg = (((b[9] - 1) & 0x0f) << 8) | ((b[10] - 1) & 0xff);
		    
            int rain_raw      = (((b[11] - 1) & 0xff) << 8) | ((b[12] - 1) & 0xff);
            float rain_mm     = rain_raw * 0.2f;
		    
            int uv_index      = (b[13] - 1) & 0x1f;
		    
            int lux_14        = (b[14] - 1) & 0xFF;
            int lux_15        = (b[15] - 1) & 0xFF;
            int lux_multi     = ((lux_14 & 0x80) >> 7);
            int light_lux     = ((lux_14 & 0x7f) << 8) | (lux_15);
		
            if (lux_multi == 1) {
                light_lux = light_lux * 10;
            }
		
            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",                 DATA_STRING, "Lacrosse_WS6262",
                    "id",               "",                 DATA_FORMAT, "%03x", DATA_INT,    id,
                    "channel",          "Channel",          DATA_INT,    channel,
                    "battery_ok",       "Battery_OK",       DATA_INT,    !battery_low,
                    "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                    "humidity",         "Humidity",         DATA_FORMAT, "%u %%",   DATA_INT,    humidity,
                    "wind_avg_km_h",    "Wind avg speed",   DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, speed_kmh,
				    "wind_max_km_h",    "Wind max speed",   DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, gust_kmh,
                    "wind_dir_deg",     "Wind Direction",   DATA_INT,    direction_deg,
                    "rain_mm",          "Total rainfall",   DATA_FORMAT, "%.1f mm",  DATA_DOUBLE, rain_mm,
                    "uv",               "UV Index",         DATA_FORMAT, "%u", DATA_INT, uv_index,
                    "light_lux",        "Lux",              DATA_FORMAT, "%u", DATA_INT, light_lux,
                    "pairing",          "Pairing?",         DATA_COND,   pairing,    DATA_INT,    !!pairing,
                    "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
                    NULL);
            /* clang-format on */
		
			decoder_output_data(decoder, data);
			return 1;
		}
		pos += LACROSSE_WSTX_BITLEN;
    }
    return ret;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "wind_avg_km_h",
        "wind_max_km_h",
        "rain_mm",
        "wind_dir_deg",
        "uv",
        "light_lux",
        "pairing",
        "mic",
        NULL,
};

r_device const lacrosse_ws6262 = {
        .name        = "LaCrosse Technology WS6262 Weather Station - Sensor WSTX62TY",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 90,
        .long_width  = 90,
        .reset_limit = 9000,
        .decode_fn   = &lacrosse_WS6262_decode,
        .fields      = output_fields,
};
