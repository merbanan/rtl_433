/** @file
    Sainlogic SA8 Weather Station.

    Copyright (C) 2026 Bruno OCTAU (\@ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Sainlogic SA8 Weather Station.

Desciption:
- All in one Weather station, with indor display and outdor Weather sensors for Wind Speed/Gust/Direction, Temp/Humidity and Rain Gauge

Compatible rebrand:
- Gevanti SA8

FCC ID:
- 2BP5V-8SA8P

Brand from FCC ID information:
- Dong Guan Zhen Ke Technology Co., LTD - Original Equipment

S.a. issue #3445 open by \@lrbison

RF Information:
- 433.92 Mhz, OOK PCM signal, UART coded.
- flex decoder:

    rtl_433 -X 'n=SA8,m=OOK_PCM,s=200,l=200,r=2500,bits>=800,bits<=1100,preamble=fc95,decode_uart'

Data layout:

    Byte Position   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40
    Sample         46 54 24 cd ab 26 0c d0 bd c3 75 39 e3 39 e3 e8 44 f3 00 6f 00 3d 00 00 00 00 00 b4 00 7e 00 41 00 53 00 00 00 f1 10 17 1d
                   II II II II II II II II II II II II II II II CC CC ?? ?? TT TT HH 00 00 00 00 00 GG GG WW WW DD DD RR RR ?? ?? BB BB XX XX


- II:{120} Fixed value, ID / MAC address of the Outdor Weather Station, to be confirmed
- CC: {16} little endian LSB/MSB, Counter, +1 each message transmit
- ??: {16} fixed value 0xf300
- TT: {16} little endian LSB/MSB, signed value, Temp °C, scale 10
- HH:  {8} Humidity %
- 00: {40} Fixed value to 0
- GG: {16} little endian LSB/MSB, Wind Gust in m/s, scale 100
- WW: {16} little endian LSB/MSB, Wind Average in m/s, scale 100
- DD: {16} little endian LSB/MSB, Wind Direction in °, 0 = North, 180 = South
- RR: {16} little endian LSB/MSB, Rain Gauge in mm scale 0.42893617f
- ??: {16} little endian LSB/MSB, another unknown counter
- BB: {16} little endian LSB/MSB, looks battery level in mV. From first byte, battery flags 0x10 = battery OK, 0x01 = battery KO or missing
- XX: {16} CRC or Checksum, not yet guessed

*/

static int sainlogic_sa8_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{

    uint8_t const preamble_pattern[] = {0xfc, 0x95};
    uint8_t b[41];

    if (bitbuffer->num_rows != 1) {
        decoder_logf(decoder, 2, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_ABORT_EARLY;
    }

    unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 16) + 16;

    if (offset >= bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 2, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    int num_bits = bitbuffer->bits_per_row[0] - offset;
    num_bits = MIN((size_t)num_bits, sizeof (b) * 10);
    int len = extract_bytes_uart(bitbuffer->bb[0], offset, num_bits, b);

    if (len < 41) {
        decoder_log(decoder, 2, __func__, "Message too short");
        return DECODE_ABORT_LENGTH;
    }

    decoder_log_bitrow(decoder, 1, __func__, b, sizeof (b) * 8, "UART decoded MSG");

    // crc checksum here when guessed

    // ID b[0] to b[14], 15 bytes
    char ID[15 * 2 + 1];
    bitrow_snprint(b, 15 * 8, ID, sizeof (ID));
    // counter 16 15
    uint16_t counter  = b[16] << 8 | b[15];
    int temp_raw      = b[20] << 8 | b[19];
    int humidity      = b[21];
    int gust_raw      = b[28] << 8 | b[27];
    int wind_raw      = b[30] << 8 | b[29];
    int dir_degree    = b[32] << 8 | b[31];
    uint16_t rain_raw = b[34] << 8 | b[33];
    uint16_t unknown  = b[36] << 8 | b[35]; // may be rain/h counter
    int battery_ok    = (b[38] & 0x10) >> 4;
    uint16_t bat_mv   = b[38] << 8 | b[37];
    //int unknown_flags = b[37] << 8  | b[38]; // big endian until guessed
    //int unknown_chk   = b[39] << 8  | b[40]; // big endian until guessed

    float temp_c      = temp_raw * 0.1f;
    float gust_km_h   = gust_raw * 0.036f; // orignal value is m/s but for Customary conversion, km/h is better
    float wind_km_h   = wind_raw * 0.036f; // orignal value is m/s but for Customary conversion, km/h is better
    float rain_mm     = rain_raw * 0.42893617f;

    /* clang-format off */
    data_t *data = data_make(
            "model",           "",               DATA_STRING, "Sainlogic-SA8",
            "id",              "",               DATA_STRING, ID,
            "battery_ok",      "Battery_OK",     DATA_INT,    battery_ok,
            "battery_mV",      "Battery Voltage",DATA_FORMAT, "%u mV",     DATA_INT,    bat_mv,
            "counter",         "Counter",        DATA_INT,    counter,
            "temperature_C",   "Temperature",    DATA_FORMAT, "%.1f C",    DATA_DOUBLE, temp_c,
            "humidity",        "Humidity",       DATA_FORMAT, "%u %%",     DATA_INT,    humidity,
            "wind_avg_km_h",   "Wind avg speed", DATA_FORMAT, "%.1f km/h", DATA_DOUBLE, wind_km_h,
            "wind_max_km_h",   "Wind max speed", DATA_FORMAT, "%.1f km/h", DATA_DOUBLE, gust_km_h,
            "wind_dir_deg",    "Wind Direction", DATA_INT,    dir_degree,
            "rain_mm",         "Total rainfall", DATA_FORMAT, "%.1f mm",   DATA_DOUBLE, rain_mm,
            "unknown",         "Unknown",        DATA_FORMAT, "%04x",      DATA_INT,    unknown,
            //"flags",           "Flags",          DATA_FORMAT, "%04x",      DATA_INT,    unknown_flags,
            "mic",             "Integrity",      DATA_STRING, "NO CHECKSUM", //until formula is guessed
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "battery_mV",
        "counter",
        "temperature_C",
        "humidity",
        "wind_avg_m_s",
        "wind_max_m_s",
        "wind_dir_deg",
        "rain_mm",
        "unknown",
        //"flags",
        "mic",
        NULL,
};

r_device const sainlogic_sa8 = {
        .name        = "Sainlogic SA8, Gevanti SA8 Weather Station",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 200,
        .long_width  = 200,
        .reset_limit = 2500,
        .decode_fn   = &sainlogic_sa8_decode,
        .disabled    = 1, // no MIC, disabled by default
        .fields      = output_fields,
};
