/** @file
    LaCrosse Technology View TX22U-IT temperature, humidity, wind speed/direction
    and rain sensor.

    Copyright (C) 2020 Caz Yokoyama, caz at caztech dot com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
LaCrosse Technology TX22U-IT temperature, humidity, wind speed/direction
and rain sensor.

LaCrosse Color Forecast Station (model WS-1611-IT) utilizes the remote
Thermo/Hygro/Wind/Rain TX22U-IT multi sensor transmitting in the 915 MHz band.

Product pages:
https://www.lacrossetechnology.com/products/ws-1611-it

Specifications:
- Wind Speed Range: 0 to 178 km/h
- Degrees of Direction: 360 deg with 16 Cardinal Directions
- Outdoor Temperature Range: -29 C to 60 C
- Outdoor Humidity Range: 10 to 99 %RH
- Update Interval: Every 31 Seconds

No internal inspection of the sensor was performed so can only
speculate that the remote sensor utilizes a HopeRF CMT2119A ISM
transmitter chip which is tuned to 915 MHz.

(http://www.cmostek.com/download/CMT2119A_v0.95.pdf)

Protocol Specification:

Data bits are FSK_PCM encoded with logical 1 and 0 bits 116 us in length.

Checksum is CRC-8 poly 0x31 init 0x00 over all bytes following the sync word.

After power-on the transmitter sends a full 5-quartet acquisition-phase packet
every ~4.5 seconds for approximately 5 hours. Thereafter, shorter packets
containing only 1-3 quartets are sent every 13-14 seconds.

Each quartet is a 2-byte pair where the high nibble of the first byte is the
type indicator and the remaining 12 bits carry the data value.

Acquisition phase (full packet):

    SYNC:24h ID:8h FLAGS:8h Q1_TYPE:4h Q1_DATA:12d Q2_TYPE:4h Q2_DATA:12d Q3_TYPE:4h Q3_DATA:12d Q4_TYPE:4h Q4_DATA:12d Q5_TYPE:4h Q5_DATA:12d CRC:8h END:8h

Post-acquisition (shorter packets, 1-3 quartets):

    SYNC:24h ID:8h FLAGS:8h Q1_TYPE:4h Q1_DATA:12d ... CRC:8h END:8h

Quartet types:
- 0: temperature, 3 nibbles BCD coded tenths of C plus 400 (e.g. 0x628 -> 22.8 C)
- 1: humidity, 3 nibbles BCD coded percent (e.g. 0x033 -> 33 %RH)
- 2: rain, 3 nibbles counter of contact closures (scale factor 0.518 mm)
- 3: wind, first nibble direction (x 22.5 for degrees), next two nibbles speed in 0.1 km/h
- 4: wind gust, 3 nibbles speed in 0.1 km/h

Test messages (Caz Yokoyama, acquisition phase):

    {152} aa aa 2d d4 a2 a5 05 72 10 58 20 00 38 00 40 00 fc 00 00
    -> ID:a2 flags:a5 temp:17.2C hum:58% rain:0.00mm wind:0.0km/h dir:180 gust:0.0km/h

Test messages (Caz Yokoyama, post-acquisition):

    {120} aa aa 2d d4 a2 83 10 72 20 1c 38 00 33 00 00
    -> ID:a2 flags:83 hum:72% rain:14.50mm wind:0.0km/h dir:67

    {104} aa aa 2d d4 a2 82 04 89 20 1c 70 00 00
    -> ID:a2 flags:82 temp:8.9C rain:14.50mm

    {88} aa aa 2d d4 a2 81 20 1c f7 00 00
    -> ID:a2 flags:81 wind:dir:180 speed:0.0km/h

Note: The older TX22U IT+ protocol described at nikseresht.com/blog/?p=99 uses a
different nibble-organized format with 8.621 kbps bit rate and no 0x2dd4 sync word.
That protocol is NOT compatible with this decoder.
*/

#include "decoder.h"
#define BIT_PER_BYTE 8

static int decode_3bcd(uint8_t *p)
{
    return ((*p & 0x0f) * 100) + ((*(p + 1) >> 4) * 10) + (*(p + 1) & 0x0f);
}

static int decode_3nybble(uint8_t *p)
{
    return ((*p & 0x0f) << 8) | *(p + 1);
}

static int lacrosse_tx22uit_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = { 0xaa, 0x2d, 0xd4 };

    data_t *data;
    uint8_t b[13], *p;
    uint32_t id;
    int flags, offset, chk, size;
    int raw_temp = -1, humidity = -1, raw_speed = -1, direction = -1;
    float temp_c, rain_mm = -1.0, speed_kmh, wind_gust_kmh = -1.0;

    offset = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    offset += sizeof(preamble_pattern) * BIT_PER_BYTE;
    size = bitbuffer->bits_per_row[0] - offset;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, size);
    size /= BIT_PER_BYTE;

    chk = crc8(b, size, 0x31, 0x00);
    if (chk) {
        decoder_log(decoder, 1, __func__, "CRC failed!");
        return DECODE_FAIL_MIC;
    }

    if (decoder->verbose)
        bitbuffer_print(bitbuffer);

    p = b;
    id = *p++;
    flags = *p++;
    for (; p < &b[size] - 2; p += 2) { /* the last 2 byte are checksum and 0x00 */
        switch (*p >> 4) {
        case 0: /* temperature */
            raw_temp = decode_3bcd(p);
            break;
        case 1: /* humidity  */
            humidity = decode_3bcd(p);
            break;
        case 2:
            /*
              When rain_mm is 14.50mm, corresponding display. WS-1611-IT shows
              14.0mm. So the display memorizes the value and shows appropriately.
             */
            rain_mm = 0.5180 * decode_3nybble(p);
            break;
        case 3: /* wind */
            direction = (*p & 0x0f) * 22.5;
            raw_speed = *(p + 1);
            break;
        case 4:
            /* TODO: Is this really wind gust? */
            wind_gust_kmh = decode_3nybble(p) * 0.1;
            break;
        default:
            /* Unknown report type, skip */
            break;
        }
    }

    // base and/or scale adjustments
    temp_c = (raw_temp - 400) * 0.1f;
    speed_kmh = raw_speed * 0.1f;

    /* clang-format off */
    data = data_make(
        "model",          "",               DATA_STRING, "LaCrosse-TX22UIT",
        "id",             "Sensor ID",      DATA_FORMAT, "%02x", DATA_INT, id,
        "flags",          "flags",          DATA_FORMAT, "%02x", DATA_INT, flags,
        "temperature_C",  "Temperature",    DATA_COND, -40.0 < temp_c && temp_c <= 70.0,
            DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
        "humidity",       "Humidity",       DATA_COND, 0 < humidity && humidity <= 100,
            DATA_FORMAT, "%u %%", DATA_INT, humidity,
        "rain_mm",       "Rainfall",        DATA_COND, 0.0 <= rain_mm && rain_mm <= (0xfff * 0.5180),
            DATA_FORMAT,  "%3.2f mm", DATA_DOUBLE, rain_mm,
        "wind_avg_km_h", "Wind speed",      DATA_COND, 0.0 <= speed_kmh && speed_kmh <= 200.0,
            DATA_FORMAT, "%.1f km/h", DATA_DOUBLE, speed_kmh,
        "wind_gust_km_h", "Wind gust",      DATA_COND, 0.0 <= wind_gust_kmh && wind_gust_kmh <= 200.0,
            DATA_FORMAT, "%.1f km/h", DATA_DOUBLE, wind_gust_kmh,
        "wind_dir_deg",   "Wind direction", DATA_COND, 0 <= direction && direction <= 360,
            DATA_INT,    direction,
        "mic",            "Integrity",      DATA_STRING, "CRC",
        NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "flags",
        "temperature_C",
        "humidity",
        "rain_mm",
        "wind_avg_km_h",
        "wind_gust_km_h",
        "wind_dir_deg",
        "mic",
        NULL,
};

r_device lacrosse_tx22uit = {
        .name        = "LaCrosse Technology TX22U-IT",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 116,
        .long_width  = 116,
        .reset_limit = 5900,
        .decode_fn   = &lacrosse_tx22uit_decode,
        .fields      = output_fields,
};
