/* Fine Offset Electronics sensor protocol
 *
 * Copyright (C) 2017 Tommy Vestermark
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "data.h"
#include "util.h"
#include "pulse_demod.h"

/*
 * Fine Offset Electronics WH2 Temperature/Humidity sensor protocol
 * aka Agimex Rosenborg 66796 (sold in Denmark)
 * aka ClimeMET CM9088 (Sold in UK)
 * aka TFA Dostmann/Wertheim 30.3157 (Temperature only!) (sold in Germany)
 * aka ...
 *
 * The sensor sends two identical packages of 48 bits each ~48s. The bits are PWM modulated with On Off Keying
 *
 * The data is grouped in 6 bytes / 12 nibbles
 * [pre] [pre] [type] [id] [id] [temp] [temp] [temp] [humi] [humi] [crc] [crc]
 * There is an extra, unidentified 7th byte in WH2A packages.
 *
 * pre is always 0xFF
 * type is always 0x4 (may be different for different sensor type?)
 * id is a random id that is generated when the sensor starts
 * temp is 12 bit signed magnitude scaled by 10 celsius
 * humi is 8 bit relative humidity percentage
 *
 * Based on reverse engineering with gnu-radio and the nice article here:
 *  http://lucsmall.com/2012/04/29/weather-station-hacking-part-2/
 */
static int fineoffset_WH2_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    uint8_t b[6] = {0};
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];

    char *model;
    int type;
    uint8_t id;
    int16_t temp;
    float temperature;
    uint8_t humidity;

    if (bitbuffer->bits_per_row[0] == 48 &&
            bb[0][0] == 0xFF) { // WH2
        bitbuffer_extract_bytes(bitbuffer, 0, 8, b, 40);
        model = "Fine Offset Electronics, WH2 Temperature/Humidity sensor";

    } else if (bitbuffer->bits_per_row[0] == 55 &&
            bb[0][0] == 0xFE) { // WH2A
        bitbuffer_extract_bytes(bitbuffer, 0, 7, b, 48);
        model = "Fine Offset WH2A sensor";

    } else if (bitbuffer->bits_per_row[0] == 47 &&
            bb[0][0] == 0xFE) { // WH5
        bitbuffer_extract_bytes(bitbuffer, 0, 7, b, 40);
        model = "Fine Offset WH5 sensor";

    } else if (bitbuffer->bits_per_row[0] == 49 &&
            bb[0][0] == 0xFF && (bb[0][1]&0x80) == 0x80) { // Telldus
        bitbuffer_extract_bytes(bitbuffer, 0, 9, b, 40);
        model = "Telldus/Proove thermometer";

    } else
        return 0;

    // Validate package
    if (b[4] != crc8(&b[0], 4, 0x31, 0)) // x8 + x5 + x4 + 1 (x8 is implicit)
        return 0;

    // Nibble 2 contains type, must be 0x04 -- or is this a (battery) flag maybe? please report.
    type = b[0] >> 4;
    if (type != 4) {
        if (debug_output) {
            fprintf(stderr, "%s: Unknown type: %d\n", model, type);
        }
        return 0;
    }

    // Nibble 3,4 contains id
    id = ((b[0]&0x0F) << 4) | ((b[1]&0xF0) >> 4);

    // Nibble 5,6,7 contains 12 bits of temperature
    temp = ((b[1] & 0x0F) << 8) | b[2];
    if (bitbuffer->bits_per_row[0] != 47) { // WH2, Telldus, WH2A
        // The temperature is signed magnitude and scaled by 10
        if (temp & 0x800) {
            temp &= 0x7FF;	// remove sign bit
            temp = -temp;	// reverse magnitude
        }
    } else { // WH5
        // The temperature is unsigned offset by 40 C and scaled by 10
        temp -= 400;
    }
    temperature = temp * 0.1;

    // Nibble 8,9 contains humidity
    humidity = b[3];

    /* Get time now */
    local_time_str(0, time_str);

    // Thermo
    if (b[3] == 0xFF) {
        data = data_make("time",          "",            DATA_STRING, time_str,
                         "model",         "",            DATA_STRING, model,
                         "id",            "ID",          DATA_INT, id,
                         "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
                         "mic",           "Integrity",   DATA_STRING, "CRC",
                          NULL);
        data_acquired_handler(data);
    }
    // Thermo/Hygro
    else {
        data = data_make("time",          "",            DATA_STRING, time_str,
                         "model",         "",            DATA_STRING, model,
                         "id",            "ID",          DATA_INT, id,
                         "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
                         "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                         "mic",           "Integrity",   DATA_STRING, "CRC",
                          NULL);
        data_acquired_handler(data);
    }
    return 1;
}

/* Fine Offset Electronics WH24, HP1000 and derivatives Temperature/Humidity/Pressure sensor protocol
 *
 * The sensor sends a package each ~16 s with a width of ~11 ms. The bits are PCM modulated with Frequency Shift Keying
 *
 * Example:
 *      [00] {196} d5 55 55 55 55 16 ea 12 5f 85 71 03 27 04 01 00 25 00 00 80 00 00 47 83 90
 *   aligned {192} aa aa aa aa aa 2d d4 24 bf 0a e2 06 4e 08 02 00 4a 00 01 00 00 00 8f 07
 * Reading: id: 191, temp: 11.8 C, humidity: 78 %, wind_dir 266 deg, wind_speed: 1.12 m/s, gust_speed 2.24 m/s, rainfall: 22.2 mm
 *
 * Preamble:  aa aa aa aa aa
 * Sync word: 2d d4
 * Payload:   FF II DD VT TT HH WW GG RR RR UU UU LL LL LL CC BB
 *
 * FF = Family Code, fixed 0x24
 * II = Sensor ID, set on battery change
 * DD = Wind direction
 * V = Various bits, D11S, wind dir 8th bit, wind speed 8th bit
 * T TT = Temperature (+40*10), top bit is low battery flag
 * HH = Humidity
 * WW = Wind speed
 * GG = Gust speed
 * RR RR = rainfall counter
 * UU UU = UV value
 * LL LL LL = light value
 * CC = CRC checksum of the 15 data bytes
 * BB = Bitsum (sum without carry, XOR) of the 16 data bytes
 */
static int fineoffset_WH24_callback(bitbuffer_t *bitbuffer)
{
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    static uint8_t const preamble[] = {0xAA, 0x2D, 0xD4}; // part of preamble and sync word
    uint8_t b[17]; // aligned packet data
    unsigned bit_offset;

    // Validate package, WH24 nominal size is 196 bit periods, WH65b is 209 bit periods
    if (bitbuffer->bits_per_row[0] < 190 || bitbuffer->bits_per_row[0] > 215) {
        return 0;
    }

    // Find a data package and extract data buffer
    bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8;
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) { // Did not find a big enough package
        if (debug_output) {
            fprintf(stderr, "Fineoffset_WH24: short package. Header index: %u\n", bit_offset);
            bitbuffer_print(bitbuffer);
        }
        return 0;
    }
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    if (debug_output) {
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
        if (debug_output) {
            fprintf(stderr, "Fineoffset_WH24: Checksum error: %02x %02x\n", crc, checksum);
        }
        return 0;
    }

    // Decode data
    int id              = b[1];                      // changes on battery change
    int wind_dir        = b[2] | (b[3] & 0x80) << 1; // range 0-359 deg, 0x1ff if invalid
    int low_battery     = (b[3] & 0x08) >> 3;
    int temp_raw        = (b[3] & 0x07) << 8 | b[4]; // 0x7ff if invalid
    float temperature   = temp_raw * 0.1 - 40.0; // range -40.0-60.0 C
    int humidity        = b[5];                      // 0xff if invalid
    int wind_speed_raw  = b[6] | (b[3] & 0x10) << 4; // 0x1ff if invalid
    // Wind speed is scaled by 8, wind speed = raw / 8 * 1.12 m/s 
    float wind_speed_ms = wind_speed_raw * 0.125 * 1.12;
    int gust_speed_raw  = b[7];             // 0xff if invalid
    // Wind gust is unscaled, multiply by wind speed factor 1.12 m/s
    float gust_speed_ms = gust_speed_raw * 1.12;
    int rainfall_raw    = b[8] << 8 | b[9]; // rain tip counter
    float rainfall_mm   = rainfall_raw * 0.3; // each tip is 0.3mm
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
    local_time_str(0, time_str);
    data = data_make(
            "time",             "",                 DATA_STRING, time_str,
            "model",            "",                 DATA_STRING, "Fine Offset WH24",
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
    data_acquired_handler(data);

    return 1;
}

/* Fine Offset Electronics WH25 Temperature/Humidity/Pressure sensor protocol
 *
 * The sensor sends a package each ~64 s with a width of ~28 ms. The bits are PCM modulated with Frequency Shift Keying
 *
 * Example:
 * [00] {500} 80 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 2a aa aa aa aa aa 8b 75 39 40 9c 8a 09 c8 72 6e ea aa aa 80 10
 * Reading: 22.6 C, 40 %, 1001.7 hPa
 *
 * Extracted data:
 *          ?I IT TT HH PP PP CC BB
 * aa 2d d4 e5 02 72 28 27 21 c9 bb aa
 *
 * II = Sensor ID (based on 2 different sensors). Does not change at battery change.
 * T TT = Temperature (+40*10), top bit is low battery flag
 * HH = Humidity
 * PP PP = Pressure (*10)
 * CC = Checksum of previous 6 bytes (binary sum truncated to 8 bit)
 * BB = Bitsum (XOR) of the 6 data bytes (high and low nibble exchanged)
 *
 */
static int fineoffset_WH25_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    static uint8_t const preamble[] = {0xAA, 0x2D, 0xD4};
    uint8_t b[8];
    unsigned bit_offset;

    // Validate package
    if (bitbuffer->bits_per_row[0] < 440 || bitbuffer->bits_per_row[0] > 510) {  // Nominal size is 488 bit periods
        return fineoffset_WH24_callback(bitbuffer); // abort and try WH24
    }

    // Find a data package and extract data payload
    bit_offset = bitbuffer_search(bitbuffer, 0, 320, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8; // Normal index is 367, skip some bytes to find faster
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        if (debug_output) {
            fprintf(stderr, "Fineoffset_WH25: short package. Header index: %u\n", bit_offset);
            bitbuffer_print(bitbuffer);
        }
        return 0;
    }
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8);

    if (debug_output) {
        char raw_str[8 * 3 + 1];
        for (unsigned n=0; n<sizeof(b); n++) { sprintf(raw_str+n*3, "%02x ", b[n]); }
        fprintf(stderr, "Fineoffset_WH25: Raw: %s @ bit_offset [%u]\n", raw_str, bit_offset);
    }

    // Verify checksum
    uint8_t checksum = 0, bitsum = 0;
    for (unsigned n = 0; n < 6; ++n) {
        checksum += b[n];
        bitsum ^= b[n];
    }
    bitsum = (bitsum << 4) | (bitsum >> 4);     // Swap nibbles
    if (checksum != b[6] || bitsum != b[7]) {
        if (debug_output) {
            fprintf(stderr, "Fineoffset_WH25: Checksum error: %02x %02x\n", checksum, bitsum);
        }
        return 0;
    }

    // Decode data
    uint8_t id        = (b[0] << 4) | (b[1] >> 4);
    int low_battery   = (b[1] & 0x08) >> 3;
    int temp_raw      = (b[1] & 0x07) << 8 | b[2]; // 0x7ff if invalid
    float temperature = temp_raw * 0.1 - 40.0;     // range -40.0-60.0 C
    uint8_t humidity  = b[3];
    float pressure    = (b[4] << 8 | b[5]) * 0.1;

    // Output data
    local_time_str(0, time_str);
    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, "Fine Offset Electronics, WH25",
                     "id",            "ID",          DATA_INT, id,
                     "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
                     "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                     "pressure_hPa",  "Pressure",    DATA_FORMAT, "%.01f hPa", DATA_DOUBLE, pressure,
                     "battery",       "Battery",     DATA_STRING, low_battery ? "LOW" : "OK",
                     "mic",           "Integrity",   DATA_STRING, "CHECKSUM",
                      NULL);
    data_acquired_handler(data);

    return 1;
}

/* Fine Offset Electronics WH0530 Temperature/Rain sensor protocol
 * aka Agimex Rosenborg 35926 (sold in Denmark)
 * aka ...
 *
 * The sensor sends two identical packages of 71 bits each ~48s. The bits are PWM modulated with On Off Keying
 * Data consists of 9 bytes with first bit missing
 *
 * Extracted data:
 * 7f 38 a2 8f 02 00 ff e7 51
 * hh hI IT TT RR RR ?? CC CC
 *
 * hh h = Header (first bit is not received and must be added)
 * II = Sensor ID (guess). Does not change at battery change.
 * T TT = Temperature (+40*10) (Upper bit is Battery Low indicator)
 * RR RR = Rain count (each count = 0.3mm, LSB first)
 * ?? = Always 0xFF (maybe reserved for humidity?)
 * CC = CRC8 with polynomium 0x31
 * CC = Checksum of previous 7 bytes (binary sum truncated to 8 bit)
 */
static int fineoffset_WH0530_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    bitrow_t *bb = bitbuffer->bb;

    // Validate package
    if (bitbuffer->bits_per_row[0] != 71        // Match exact length to avoid false positives
            || (bb[0][0]>>1) != 0x7F            // Check header (two upper nibbles)
            || (bb[0][1]>>5) != 0x3)            // Check header (third nibble)
        return 0;

    uint8_t buffer[8];
    bitbuffer_extract_bytes(bitbuffer, 0, 7, buffer, sizeof(buffer) * 8);     // Skip first 7 bits

    if (debug_output) {
        char raw_str[8 * 3 + 1];
        for (unsigned n=0; n<sizeof(buffer); n++) { sprintf(raw_str + n * 3, "%02x ", buffer[n]); }
        fprintf(stderr, "Fineoffset_WH0530: Raw %s\n", raw_str);
    }

    // Verify checksum
    uint8_t crc = crc8(buffer, 6, 0x31, 0);
    uint8_t checksum = buffer[0] + buffer[1] + buffer[2] + buffer[3] + buffer[4] + buffer[5] + buffer[6];
    if (crc != buffer[6] || checksum != buffer[7]) {
        if (debug_output) {
            fprintf(stderr, "Fineoffset_WH0530: Checksum error: %02x %02x\n", crc, checksum);
        }
        return 0;
    }

    uint8_t id = (buffer[0]<<4) | (buffer[1]>>4);
    uint8_t battery_low = (buffer[1] & 0x8);
    float temperature = ((buffer[1] & 0x7)<< 8 | buffer[2]) * 0.1 - 40.0;
    int rainfall_raw = buffer[4] << 8 | buffer[3]; // rain tip counter
    float rainfall = rainfall_raw * 0.3; // each tip is 0.3mm

    local_time_str(0, time_str);
    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, "Fine Offset Electronics, WH0530 Temperature/Rain sensor",
                     "id",            "ID",          DATA_INT, id,
                     "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
                     "rain",          "Rain",        DATA_FORMAT, "%.01f mm", DATA_DOUBLE, rainfall,
                     "battery",       "Battery",     DATA_STRING, battery_low ? "LOW" : "OK",
                     "mic",           "Integrity",   DATA_STRING, "CRC",
                     NULL);
    data_acquired_handler(data);

    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "temperature_C",
    "humidity",
    "mic",
    NULL
};

static char *output_fields_WH25[] = {
    "time",
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
    "time",
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
    .modulation     = OOK_PULSE_PWM_PRECISE,
    .short_limit    = 500,	// Short pulse 544µs, long pulse 1524µs, fixed gap 1036µs
    .long_limit     = 1500,	// Maximum pulse period (long pulse + fixed gap)
    .reset_limit    = 1200,	// We just want 1 package
    .tolerance      = 160, // us
    .json_callback  = &fineoffset_WH2_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields
};

r_device fineoffset_WH25 = {
    .name           = "Fine Offset Electronics, WH25, WH24, HP1000 Temperature/Humidity/Pressure Sensor",
    .modulation     = FSK_PULSE_PCM,
    .short_limit    = 58,	// Bit width = 58µs (measured across 580 samples / 40 bits / 250 kHz )
    .long_limit     = 58,	// NRZ encoding (bit width = pulse width)
    .reset_limit    = 20000,	// Package starts with a huge gap of ~18900 us
    .json_callback  = &fineoffset_WH25_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields_WH25
};

r_device fineoffset_WH0530 = {
    .name           = "Fine Offset Electronics, WH0530 Temperature/Rain Sensor",
    .modulation     = OOK_PULSE_PWM_PRECISE,
    .short_limit    = 504,	// Short pulse 504µs
    .long_limit     = 1480, // Long pulse 1480µs
    .reset_limit    = 1200,	// Fixed gap 960µs (We just want 1 package)
    .sync_width     = 0,    // No sync bit used
    .tolerance      = 160, // us
    .json_callback  = &fineoffset_WH0530_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields_WH0530
};
