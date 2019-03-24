/** @file
    Fine Offset Electronics sensor protocol.

    Copyright (C) 2017 Tommy Vestermark
    Enhanced (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"
#include <stdlib.h>

r_device fineoffset_WH2;

static r_device *fineoffset_WH2_create(char *arg)
{
    r_device *r_dev = create_device(&fineoffset_WH2);

    if (arg && !strcmp(arg, "no-wh5")) {
        int *quirk = malloc(sizeof (*quirk));
        *quirk = 1;
        r_dev->decode_ctx = quirk;
    }

    return r_dev;
}

/**
Fine Offset Electronics WH2 Temperature/Humidity sensor protocol,
also Agimex Rosenborg 66796 (sold in Denmark), collides with WH5,
also ClimeMET CM9088 (Sold in UK),
also TFA Dostmann/Wertheim 30.3157 (Temperature only!) (sold in Germany).

The sensor sends two identical packages of 48 bits each ~48s. The bits are PWM modulated with On Off Keying.

The data is grouped in 6 bytes / 12 nibbles.

    [pre] [pre] [type] [id] [id] [temp] [temp] [temp] [humi] [humi] [crc] [crc]

There is an extra, unidentified 7th byte in WH2A packages.

- pre is always 0xFF
- type is always 0x4 (may be different for different sensor type?)
- id is a random id that is generated when the sensor starts
- temp is 12 bit signed magnitude scaled by 10 celsius
- humi is 8 bit relative humidity percentage

Based on reverse engineering with gnu-radio and the nice article here:
http://lucsmall.com/2012/04/29/weather-station-hacking-part-2/
*/
static int fineoffset_WH2_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    uint8_t b[6] = {0};
    data_t *data;

    char *model;
    int type;
    uint8_t id;
    int16_t temp;
    float temperature;
    uint8_t humidity;

    if (bitbuffer->bits_per_row[0] == 48 &&
            bb[0][0] == 0xFF) { // WH2
        bitbuffer_extract_bytes(bitbuffer, 0, 8, b, 40);
        model = _X("Fineoffset-WH2","Fine Offset Electronics, WH2 Temperature/Humidity sensor");

    } else if (bitbuffer->bits_per_row[0] == 55 &&
            bb[0][0] == 0xFE) { // WH2A
        bitbuffer_extract_bytes(bitbuffer, 0, 7, b, 48);
        model = _X("Fineoffset-WH2A","Fine Offset WH2A sensor");

    } else if (bitbuffer->bits_per_row[0] == 47 &&
            bb[0][0] == 0xFE) { // WH5
        bitbuffer_extract_bytes(bitbuffer, 0, 7, b, 40);
        model = _X("Fineoffset-WH5","Fine Offset WH5 sensor");
        if (decoder->decode_ctx) // don't care for the actual value
            model = "Rosenborg-66796";

    } else if (bitbuffer->bits_per_row[0] == 49 &&
            bb[0][0] == 0xFF && (bb[0][1]&0x80) == 0x80) { // Telldus
        bitbuffer_extract_bytes(bitbuffer, 0, 9, b, 40);
        model = _X("Fineoffset-TelldusProove","Telldus/Proove thermometer");

    } else
        return 0;

    // Validate package
    if (b[4] != crc8(&b[0], 4, 0x31, 0)) // x8 + x5 + x4 + 1 (x8 is implicit)
        return 0;

    // Nibble 2 contains type, must be 0x04 -- or is this a (battery) flag maybe? please report.
    type = b[0] >> 4;
    if (type != 4) {
        if (decoder->verbose)
            fprintf(stderr, "%s: Unknown type: %d\n", model, type);
        return 0;
    }

    // Nibble 3,4 contains id
    id = ((b[0]&0x0F) << 4) | ((b[1]&0xF0) >> 4);

    // Nibble 5,6,7 contains 12 bits of temperature
    temp = ((b[1] & 0x0F) << 8) | b[2];
    if (bitbuffer->bits_per_row[0] != 47 || decoder->decode_ctx) { // WH2, Telldus, WH2A
        // The temperature is signed magnitude and scaled by 10
        if (temp & 0x800) {
            temp &= 0x7FF; // remove sign bit
            temp = -temp; // reverse magnitude
        }
    } else { // WH5
        // The temperature is unsigned offset by 40 C and scaled by 10
        temp -= 400;
    }
    temperature = temp * 0.1;

    // Nibble 8,9 contains humidity
    humidity = b[3];

    // Thermo
    if (b[3] == 0xFF) {
        data = data_make(
                "model",            "",             DATA_STRING, model,
                "id",               "ID",           DATA_INT, id,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
                "mic",              "Integrity",    DATA_STRING, "CRC",
                NULL);
        decoder_output_data(decoder, data);
    }
    // Thermo/Hygro
    else {
        data = data_make(
                "model",            "",             DATA_STRING, model,
                "id",               "ID",           DATA_INT, id,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
                "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
                "mic",              "Integrity",    DATA_STRING, "CRC",
                NULL);
        decoder_output_data(decoder, data);
    }
    return 1;
}

/**
Fine Offset Electronics WH24, WH65B, HP1000 and derivatives Temperature/Humidity/Pressure sensor protocol.

The sensor sends a package each ~16 s with a width of ~11 ms. The bits are PCM modulated with Frequency Shift Keying.

Example:

         [00] {196} d5 55 55 55 55 16 ea 12 5f 85 71 03 27 04 01 00 25 00 00 80 00 00 47 83 9
      aligned {199} 1aa aa aa aa aa 2d d4 24 bf 0a e2 06 4e 08 02 00 4a 00 01 00 00 00 8f 07 2
    Payload:                              FF II DD VT TT HH WW GG RR RR UU UU LL LL LL CC BB
    Reading: id: 191, temp: 11.8 C, humidity: 78 %, wind_dir 266 deg, wind_speed: 1.12 m/s, gust_speed 2.24 m/s, rainfall: 22.2 mm

The WH65B sends the same data with a slightly longer preamble and postamble

            {209} 55 55 55 55 55 51 6e a1 22 83 3f 14 3a 08 00 00 00 08 00 10 00 00 04 60 a1 00 8
    aligned  {208} a aa aa aa aa aa 2d d4 24 50 67 e2 87 41 00 00 00 01 00 02 00 00 00 8c 14 20 1
    Payload:                              FF II DD VT TT HH WW GG RR RR UU UU LL LL LL CC BB

- Preamble:  aa aa aa aa aa
- Sync word: 2d d4
- Payload:   FF II DD VT TT HH WW GG RR RR UU UU LL LL LL CC BB

- F: 8 bit Family Code, fixed 0x24
- I: 8 bit Sensor ID, set on battery change
- D: 8 bit Wind direction
- V: 4 bit Various bits, D11S, wind dir 8th bit, wind speed 8th bit
- B: 1 bit low battery indicator
- T: 11 bit Temperature (+40*10), top bit is low battery flag
- H: 8 bit Humidity
- W: 8 bit Wind speed
- G: 8 bit Gust speed
- R: 16 bit rainfall counter
- U: 16 bit UV value
- L: 24 bit light value
- C: 8 bit CRC checksum of the 15 data bytes
- B: 8 bit Bitsum (sum without carry, XOR) of the 16 data bytes
 */
#define MODEL_WH24 24 /* internal identifier for model WH24, family code is always 0x24 */
#define MODEL_WH65B 65 /* internal identifier for model WH65B, family code is always 0x24 */
static int fineoffset_WH24_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    static uint8_t const preamble[] = {0xAA, 0x2D, 0xD4}; // part of preamble and sync word
    uint8_t b[17]; // aligned packet data
    unsigned bit_offset;
    int model;

    // Validate package, WH24 nominal size is 196 bit periods, WH65b is 209 bit periods
    if (bitbuffer->bits_per_row[0] < 190 || bitbuffer->bits_per_row[0] > 215) {
        return 0;
    }

    // Find a data package and extract data buffer
    bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8;
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) { // Did not find a big enough package
        if (decoder->verbose) {
            fprintf(stderr, "Fineoffset_WH24: short package. Header index: %u\n", bit_offset);
            bitbuffer_print(bitbuffer);
        }
        return 0;
    }
    if (bitbuffer->bits_per_row[0] - bit_offset - sizeof(b) * 8 < 8)
        model = MODEL_WH24; // nominal 3 bits postamble
    else
        model = MODEL_WH65B; // nominal 12 bits postamble
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    if (decoder->verbose) {
        char raw_str[17 * 3 + 1];
        for (unsigned n = 0; n < sizeof(b); n++) {
            sprintf(raw_str + n * 3, "%02x ", b[n]);
        }
        fprintf(stderr, "Fineoffset_WH24: Raw: %s @ bit_offset [%u]\n", raw_str, bit_offset);
    }

    if (b[0] != 0x24) // Check for family code 0x24
        return 0;

    // Verify checksum, same as other FO Stations: Reverse 1Wire CRC (poly 0x131)
    uint8_t crc = crc8(b, 15, 0x31, 0x00);
    uint8_t checksum = 0;
    for (unsigned n = 0; n < 16; ++n) {
        checksum += b[n];
    }
    if (crc != b[15] || checksum != b[16]) {
        if (decoder->verbose) {
            fprintf(stderr, "Fineoffset_WH24: Checksum error: %02x %02x\n", crc, checksum);
        }
        return 0;
    }

    // Decode data
    int id              = b[1];                      // changes on battery change
    int wind_dir        = b[2] | (b[3] & 0x80) << 1; // range 0-359 deg, 0x1ff if invalid
    int low_battery     = (b[3] & 0x08) >> 3;
    int temp_raw        = (b[3] & 0x07) << 8 | b[4]; // 0x7ff if invalid
    float temperature   = (temp_raw - 400) * 0.1; // range -40.0-60.0 C
    int humidity        = b[5];                      // 0xff if invalid
    int wind_speed_raw  = b[6] | (b[3] & 0x10) << 4; // 0x1ff if invalid
    float wind_speed_factor, rain_cup_count;
    // Wind speed factor is 1.12 m/s (1.19 per specs?) for WH24, 0.51 m/s for WH65B
    // Rain cup each count is 0.3mm for WH24, 0.01inch (0.254mm) for WH65B
    if (model == MODEL_WH24) { // WH24
        wind_speed_factor = 1.12f;
        rain_cup_count = 0.3f;
    } else { // WH65B
        wind_speed_factor = 0.51f;
        rain_cup_count = 0.254f;
    }
    // Wind speed is scaled by 8, wind speed = raw / 8 * 1.12 m/s (0.51 for WH65B)
    float wind_speed_ms = wind_speed_raw * 0.125 * wind_speed_factor;
    int gust_speed_raw  = b[7];             // 0xff if invalid
    // Wind gust is unscaled, multiply by wind speed factor 1.12 m/s
    float gust_speed_ms = gust_speed_raw * wind_speed_factor;
    int rainfall_raw    = b[8] << 8 | b[9]; // rain tip counter
    float rainfall_mm   = rainfall_raw * rain_cup_count; // each tip is 0.3mm / 0.254mm
    int uv_raw          = b[10] << 8 | b[11];               // range 0-20000, 0xffff if invalid
    int light_raw       = b[12] << 16 | b[13] << 8 | b[14]; // 0xffffff if invalid
    float light_lux     = light_raw * 0.1; // range 0.0-300000.0lux
    // Light = value/10 ; Watts/m Sqr. = Light/683 ;  Lux to W/m2 = Lux/126

    // UV value   UVI
    // 0-432      0
    // 433-851    1
    // 852-1210   2
    // 1211-1570  3
    // 1571-2017  4
    // 2018-2450  5
    // 2451-2761  6
    // 2762-3100  7
    // 3101-3512  8
    // 3513-3918  9
    // 3919-4277  10
    // 4278-4650  11
    // 4651-5029  12
    // >=5230     13
    int uvi_upper[] = {432, 851, 1210, 1570, 2017, 2450, 2761, 3100, 3512, 3918, 4277, 4650, 5029};
    int uv_index   = 0;
    while (uv_index < 13 && uvi_upper[uv_index] < uv_raw) ++uv_index;

    // Output data
    data = data_make(
            "model",            "",                 DATA_STRING, model == MODEL_WH24 ? _X("Fineoffset-WH24","Fine Offset WH24") : _X("Fineoffset-WH65B","Fine Offset WH65B"),
            "id",               "ID",               DATA_INT, id,
            NULL);
    if (temp_raw       != 0x7ff)
        data_append(data,   "temperature_C",    "Temperature",      DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature, NULL);
    if (humidity       != 0xff)
        data_append(data,   "humidity",         "Humidity",         DATA_FORMAT, "%u %%", DATA_INT, humidity, NULL);
    if (wind_dir       != 0x1ff)
        data_append(data,   "wind_dir_deg",     "Wind direction",   DATA_INT, wind_dir, NULL);
    if (wind_speed_raw != 0x1ff)
        data_append(data,   "wind_speed_ms",    "Wind speed",       DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind_speed_ms, NULL);
    if (gust_speed_raw != 0xff)
        data_append(data,   "gust_speed_ms",    "Gust speed",       DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, gust_speed_ms, NULL);
    data_append(data,       "rainfall_mm",      "Rainfall",         DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rainfall_mm, NULL);
    if (uv_raw         != 0xffff)
        data_append(data,   "uv",               "UV",               DATA_INT, uv_raw,
                            "uvi",              "UVI",              DATA_INT, uv_index, NULL);
    if (light_raw      != 0xffffff)
        data_append(data,   "light_lux",        "Light",            DATA_FORMAT, "%.1f lux", DATA_DOUBLE, light_lux, NULL);
    data_append(data,       "battery",          "Battery",          DATA_STRING, low_battery ? "LOW" : "OK",
                            "mic",              "Integrity",        DATA_STRING, "CRC", NULL);
    decoder_output_data(decoder, data);

    return 1;
}

/**
Fine Offset Electronics WH25 Temperature/Humidity/Pressure sensor protocol.

The sensor sends a package each ~64 s with a width of ~28 ms. The bits are PCM modulated with Frequency Shift Keying.

Example: 22.6 C, 40 %, 1001.7 hPa

    [00] {500} 80 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 2a aa aa aa aa aa 8b 75 39 40 9c 8a 09 c8 72 6e ea aa aa 80 10

Data layout:

    aa 2d d4 e5 02 72 28 27 21 c9 bb aa
             ?I IT TT HH PP PP CC BB

- I: 8 bit Sensor ID (based on 2 different sensors). Does not change at battery change.
- B: 1 bit low battery indicator
- T: 11 bit Temperature (+40*10), top bit is low battery flag
- H: 8 bit Humidity
- P: 16 bit Pressure (*10)
- C: 8 bit Checksum of previous 6 bytes (binary sum truncated to 8 bit)
- B: 8 bit Bitsum (XOR) of the 6 data bytes (high and low nibble exchanged)
*/
static int fineoffset_WH25_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    static uint8_t const preamble[] = {0xAA, 0x2D, 0xD4};
    uint8_t b[8];
    unsigned bit_offset;

    // Validate package
    if (bitbuffer->bits_per_row[0] < 440 || bitbuffer->bits_per_row[0] > 510) {  // Nominal size is 488 bit periods
        return fineoffset_WH24_callback(decoder, bitbuffer); // abort and try WH24, WH65B, HP1000
    }

    // Find a data package and extract data payload
    bit_offset = bitbuffer_search(bitbuffer, 0, 320, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8; // Normal index is 367, skip some bytes to find faster
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        if (decoder->verbose)
            bitbuffer_printf(bitbuffer, "Fineoffset_WH25: short package. Header index: %u\n", bit_offset);
        return 0;
    }
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    // Verify checksum
    int sum = (add_bytes(b, 6) & 0xff) - b[6];
    if (sum) {
        if (decoder->verbose)
            bitrow_printf(b, sizeof (b) * 8, "Fineoffset_WH25: Checksum error: ");
        return 0;
    }

    // Verify xor-sum
    int bitsum = xor_bytes(b, 6);
    bitsum = ((bitsum & 0x0f) << 4) | (bitsum >> 4); // Swap nibbles
    if (bitsum != b[7]) {
        if (decoder->verbose)
            bitrow_printf(b, sizeof (b) * 8, "Fineoffset_WH25: Bitsum error: ");
        return 0;
    }

    // Decode data
    uint8_t id        = ((b[0]&0x0f) << 4) | (b[1] >> 4);
    int low_battery   = (b[1] & 0x08) >> 3;
    int temp_raw      = (b[1] & 0x07) << 8 | b[2]; // 0x7ff if invalid
    float temperature = (temp_raw - 400) * 0.1;    // range -40.0-60.0 C
    uint8_t humidity  = b[3];
    float pressure    = (b[4] << 8 | b[5]) * 0.1;

    // Output data
    data = data_make(
            "model",            "",             DATA_STRING, _X("Fineoffset-WH25","Fine Offset Electronics, WH25"),
            "id",               "ID",           DATA_INT, id,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "pressure_hPa",     "Pressure",     DATA_FORMAT, "%.01f hPa", DATA_DOUBLE, pressure,
            "battery",          "Battery",      DATA_STRING, low_battery ? "LOW" : "OK",
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);

    decoder_output_data(decoder, data);
    return 1;
}

/**
Fine Offset Electronics WH0530 Temperature/Rain sensor protocol,
also Agimex Rosenborg 35926 (sold in Denmark).

The sensor sends two identical packages of 71 bits each ~48s. The bits are PWM modulated with On Off Keying.
Data consists of 7 bit preamble and 8 bytes.

Data layout:
    38 a2 8f 02 00 ff e7 51
    FI IT TT RR RR ?? CC AA

- F: 4 bit fixed message type (0x3)
- I: 8 bit Sensor ID (guess). Does not change at battery change.
- B: 1 bit low battery indicator
- T: 11 bit Temperature (+40*10) (Upper bit is Battery Low indicator)
- R: 16 bit (little endian) rain count in 0.3 mm steps, absolute with wrap around at 65536
- ?: 8 bit Always 0xFF (maybe reserved for humidity?)
- C: 8 bit CRC-8 with poly 0x31 init 0x00
- A: 8 bit Checksum of previous 7 bytes (addition truncated to 8 bit)
*/
static int fineoffset_WH0530_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    bitrow_t *bb = bitbuffer->bb;
    uint8_t b[8];

    // Validate package
    if (bitbuffer->bits_per_row[0] != 71 // Match exact length to avoid false positives
            || (bb[0][0] >> 1) != 0x7F   // Check preamble (7 bits)
            || (bb[0][1] >> 5) != 0x3)   // Check message type (8 bits)
        return 0;

    bitbuffer_extract_bytes(bitbuffer, 0, 7, b, sizeof(b) * 8); // Skip first 7 bits

    // Verify checksum
    int crc = crc8(b, 7, 0x31, 0);
    int sum = (add_bytes(b, 7) & 0xff) - b[7];

    if (crc || sum) {
        if (decoder->verbose)
            bitrow_printf(b, sizeof (b) * 8, "Fineoffset_WH0530: Checksum error: ");
        return 0;
    }

    int id            = ((b[0] & 0x0f) << 4) | (b[1] >> 4);
    int battery_low   = (b[1] >> 3) & 0x1;
    int temp_raw      = (b[1] & 0x7) << 8 | b[2];
    float temperature = (temp_raw - 400) * 0.1;
    int rainfall_raw  = b[4] << 8 | b[3];   // rain tip counter
    float rainfall    = rainfall_raw * 0.3; // each tip is 0.3mm

    data = data_make(
            "model",            "",             DATA_STRING, _X("Fineoffset-WH0530","Fine Offset Electronics, WH0530 Temperature/Rain sensor"),
            "id",               "ID",           DATA_INT, id,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
            "rain",             "Rain",         DATA_FORMAT, "%.01f mm", DATA_DOUBLE, rainfall,
            "battery",          "Battery",      DATA_STRING, battery_low ? "LOW" : "OK",
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "temperature_C",
    "humidity",
    "mic",
    NULL
};

static char *output_fields_WH25[] = {
    "model",
    "id",
    "temperature_C",
    "humidity",
    "pressure_hPa",
    // WH24
    "wind_dir_deg",
    "wind_speed_ms",
    "gust_speed_ms",
    "rainfall_mm",
    "uv",
    "uvi",
    "light_lux",
    "battery",
    "mic",
    NULL
};

static char *output_fields_WH0530[] = {
    "model",
    "id",
    "temperature_C",
    "rain",
    "battery",
    "mic",
    NULL
};

r_device fineoffset_WH2 = {
    .name           = "Fine Offset Electronics, WH2, WH5, Telldus Temperature/Humidity/Rain Sensor",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 500, // Short pulse 544µs, long pulse 1524µs, fixed gap 1036µs
    .long_width     = 1500, // Maximum pulse period (long pulse + fixed gap)
    .reset_limit    = 1200, // We just want 1 package
    .tolerance      = 160, // us
    .decode_fn      = &fineoffset_WH2_callback,
    .create_fn      = &fineoffset_WH2_create,
    .disabled       = 0,
    .fields         = output_fields
};

r_device fineoffset_WH25 = {
    .name           = "Fine Offset Electronics, WH25, WH24, WH65B, HP1000 Temperature/Humidity/Pressure Sensor",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 58, // Bit width = 58µs (measured across 580 samples / 40 bits / 250 kHz )
    .long_width     = 58, // NRZ encoding (bit width = pulse width)
    .reset_limit    = 20000, // Package starts with a huge gap of ~18900 us
    .decode_fn      = &fineoffset_WH25_callback,
    .disabled       = 0,
    .fields         = output_fields_WH25
};

r_device fineoffset_WH0530 = {
    .name           = "Fine Offset Electronics, WH0530 Temperature/Rain Sensor",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 504, // Short pulse 504µs
    .long_width     = 1480, // Long pulse 1480µs
    .reset_limit    = 1200, // Fixed gap 960µs (We just want 1 package)
    .sync_width     = 0,    // No sync bit used
    .tolerance      = 160, // us
    .decode_fn      = &fineoffset_WH0530_callback,
    .disabled       = 0,
    .fields         = output_fields_WH0530
};
