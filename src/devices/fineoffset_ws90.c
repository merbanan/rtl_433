/** @file
    Fine Offset Electronics WS90 weather station.

    Copyright (C) 2022 Christian W. Zuckschwerdt <zany@triq.net>
    Protocol description by \@davidefa

    Copy of fineoffset_ws80.c with changes made to support Fine Offset WS90
    sensor array.  Changes made by John Pochmara <john@zoiedog.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Fine Offset Electronics WS90 weather station.

The WS90 is a WS80 with the addition of a piezoelectric rain gauge.
Data bytes 1-13 are the same between the two models.  The new rain data
is in bytes 16-20, with bytes 19 and 20 reporting total rain.  Bytes
17 and 18 are affected by rain, but it is unknown what they report.  Byte
21 reports the voltage of the super cap. And the checksum and CRC
have been moved to bytes 30 and 31.  What is reported in the other
bytes is unknown at this time.

Also sold by EcoWitt.

Preamble is aaaa aaaa aaaa, sync word is 2dd4.

Packet layout:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    YY II II II LL LL BB FF TT HH WW DD GG VV PP PP R0 R1 R2 R3 R4 SS UU UU UU UU UU UU UU ZZ AA XX
    90 00 34 2b 00 77 a4 82 62 39 00 3e 00 00 3f ff 20 00 ba 00 00 26 02 00 ff 9f f8 00 00 82 92 4f

- Y = fixed sensor type 0x90
- I = device ID, might be less than 24 bit?
- L = light value, unit of 10 lux
- B = battery voltage, unit of 20 mV, we assume a range of 3.0V to 1.4V
- F = flags and MSBs, 0x03: temp MSB, 0x10: wind MSB, 0x20: bearing MSB, 0x40: gust MSB
      0x04: temp MSB, only set for the invalid value 0x7ff
      0x80, 0x08: ultrasonic signal quality, 2 bit level (0x80 = bit 1, 0x08 = bit 0)
- T = temperature, lowest 8 bits of temperature, offset 40, scale 10
- H = humidity
- W = wind speed, lowest 8 bits of wind speed, m/s, scale 10
- D = wind bearing, lowest 8 bits of wind bearing, range 0-359 deg, 0x1ff if invalid
- G = wind gust, lowest 8 bits of wind gust, m/s, scale 10
- V = uv index, scale 10
- P = 2 bit ultrasonic status (mask 0xc000, 0 = ok, 1-3 = measurement errors),
      14 bit pressure in 0.1 hPa (mask 0x3fff), 0x3fff if no barometer is fitted
- R = rain total (R3 << 8 | R4) * 0.1 mm
- RS = rain start dection ((R0 & 0x10) >>4), 1 = raining, 0 = not raining
      (bits 0xf0 of R0 are a 4 bit counter incremented on each rain state change)
- R0 & 0x0f, R1, R2 = 20 bit rain intensity sum, unit unknown
- S = super cap voltage, unit of 0.1V, lower 6 bits, mask 0x3f
      bits 0xc0 are a rain/wet state (0-2)
- U = bytes 22-28 are rain sensor diagnostics:
      22: piezo wave count (low 8 bits), 23: piezo rain wave count (low 8 bits),
      24-26: two 12 bit ADC readings of the resistive water sensor on the dome
      (byte 24 + high nibble of 25 = sensor 2, low nibble of 25 + byte 26 = sensor 1),
      27: sensor 2 slope, 28: sensor 1 slope (signed, clamped to +/-125)
- Z = Firmware version. 0x82 = 130 = 1.3.0
- A = CRC over bytes 0-29
- X = checksum over bytes 0-30

Newer firmware (seen with 1.6.1 and 1.6.2) sends 39 bytes. The first 32 bytes are
unchanged and bytes 32-38 are appended:

    32 33 34 35 36 37 38
    OO OO EE EE EE C2 X2

- O = 14 bit uncompensated temperature (bytes 32 to 33 bit 2), 0.01 C, offset 40, 0x3fff if invalid
- E = 14 bit second temperature sensor (byte 33 bits 1-0 to byte 35 bit 4), 0.01 C, offset 40,
      0x3fff if invalid; the low nibble of byte 35 is the 0.01 C digit of the main temperature
      (0xf if invalid) and the top 3 bits of byte 36 are the active ultrasonic configuration
      (the low 5 bits are always set)
- C2 = CRC over bytes 0-36
- X2 = checksum over bytes 0-37 (often truncated at reception: the frame has no trailer, so the
       last bits get lost; the extension is then accepted on C2 alone)

The main temperature (T) and humidity are compensated for solar heating by the sensor.
Byte meanings for 1.6.2 were confirmed by analysis of the Ecowitt V1.6.2 firmware image.

Rain start info:
Status 1 will be reset to 0 when:
- Once the top is dry
- After the amount of water on the top has remained unchanged for two hours.

In the 1.6.2 firmware the rain state goes back to 0 (dry) when both resistive ADC readings are
4000 or more for 4 consecutive reports, or after 2 hours without piezo activity. It goes to 1
(wet) when an ADC reading is below 4000 and its slope magnitude is more than 29. In state 2 the
piezo pulse count (rain intensity sum) is used. Each state change increments the 4 bit counter.

*/

static int fineoffset_ws90_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xaa, 0xaa, 0x2d, 0xd4}; // 32 bit, part of preamble and sync word
    uint8_t b[39]; // 32 bytes, newer firmware appends 7 more

    // Validate package, WS90 nominal size is 345 bit periods
    if (bitbuffer->bits_per_row[0] < 168 || bitbuffer->bits_per_row[0] > 500) {
        decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "abort length");
        return DECODE_ABORT_LENGTH;
    }

    // Find a data package and extract data buffer
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, 32) + 32;
    if (bit_offset + 32 * 8 > bitbuffer->bits_per_row[0]) { // Did not find a big enough package
        decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "short package at %u (%u)", bit_offset, bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH;
    }

    // Extract package data, including the extension bytes if present
    unsigned len = (bitbuffer->bits_per_row[0] - bit_offset) / 8;
    if (len > sizeof(b))
        len = sizeof(b);
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, len * 8);

    if (b[0] != 0x90) // Check for family code 0x90
        return DECODE_ABORT_EARLY;

    decoder_logf(decoder, 1, __func__, "WS90 detected, buffer is %u bits length", bitbuffer->bits_per_row[0]);

    // Verify checksum and CRC
    uint8_t crc = crc8(b, 31, 0x31, 0x00);
    uint8_t chk = add_bytes(b, 31);
    if (crc != 0 || chk != b[31]) {
        decoder_logf(decoder, 1, __func__, "Checksum error: %02x %02x (%02x)", crc, chk, b[31]);
        return DECODE_FAIL_MIC;
    }

    // The extension bytes 32-38 have their own CRC (byte 37) and checksum (byte 38). The frame
    // has no trailer, so the last 1-5 bits are often lost by the demodulator; if the checksum
    // byte is incomplete, accept the extension on its CRC alone.
    int has_ext = len >= 38 && crc8(b, 38, 0x31, 0x00) == 0 &&
            (len < 39 || (add_bytes(b, 38) & 0xff) == b[38]);

    int id          = (b[1] << 16) | (b[2] << 8) | (b[3]);
    int light_raw   = (b[4] << 8) | (b[5]);
    float light_lux = light_raw * 10;        // Lux
    //float light_wm2 = light_raw * 0.078925f; // W/m2
    int battery_mv  = (b[6] * 20);            // mV
    int battery_lvl = battery_mv < 1400 ? 0 : (battery_mv - 1400) / 16; // 1.4V-3.0V is 0-100
    int flags       = b[7]; // to find the wind msb
    int temp_raw    = ((b[7] & 0x03) << 8) | (b[8]);
    float temp_c    = (temp_raw - 400) * 0.1f;
    int humidity    = (b[9]);
    int wind_avg    = ((b[7] & 0x10) << 4) | (b[10]);
    int wind_dir    = ((b[7] & 0x20) << 3) | (b[11]);
    int wind_max    = ((b[7] & 0x40) << 2) | (b[12]);
    int uv_index    = (b[13]);
    int pressure    = ((b[14] & 0x3f) << 8) | (b[15]); // 0.1 hPa, top 2 bits are ultrasonic status
    int rain_raw    = (b[19] << 8 ) | (b[20]);
    int rain_start  = (b[16] & 0x10) >> 4;
    int supercap_V  = (b[21] & 0x3f);
    int rain_state  = b[21] >> 6;
    int rain_changes = b[16] >> 4;
    int rain_int_sum = ((b[16] & 0x0f) << 16) | (b[17] << 8) | (b[18]);
    int piezo_waves = b[22];
    int piezo_rain  = b[23];
    int rain_adc_1  = ((b[25] & 0x0f) << 8) | (b[26]);
    int rain_adc_2  = (b[24] << 4) | (b[25] >> 4);
    int rain_slope_1 = (int8_t)b[28];
    int rain_slope_2 = (int8_t)b[27];
    int firmware    = b[29];
    int temp_raw_ext = has_ext ? (b[32] << 6) | (b[33] >> 2) : 0x3fff;                          // uncompensated
    int temp_digit  = has_ext ? b[35] & 0x0f : 0xf;                                              // 0.01 C digit
    int ult_cfg     = has_ext ? b[36] >> 5 : -1;
    int ult_status  = b[14] >> 6;
    int ult_signal  = ((b[7] & 0x80) >> 6) | ((b[7] & 0x08) >> 3);
    int temp_2_raw      = has_ext ? ((b[33] & 0x03) << 12) | (b[34] << 4) | (b[35] >> 4) : 0x3fff; // second sensor

    if (battery_lvl > 100) // More then 100%?
        battery_lvl = 100;

    char extra[31];
    snprintf(extra, sizeof(extra), "%02x%02x%02x%02x%02x------%02x%02x%02x%02x%02x%02x%02x", b[14], b[15], b[16], b[17], b[18], /* b[19,20] is the rain sensor, b[21] is supercap_V */ b[22], b[23], b[24], b[25], b[26], b[27], b[28]);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Fineoffset-WS90",
            "id",               "ID",               DATA_FORMAT, "%06x", DATA_INT,    id,
            "battery_ok",       "Battery level",    DATA_DOUBLE, battery_lvl * 0.01f,
            "battery_mV",       "Battery Voltage",  DATA_FORMAT, "%d mV", DATA_INT,    battery_mv,
            "temperature_C",    "Temperature",      DATA_COND, temp_raw != 0x3ff,   DATA_FORMAT, "%.1f C",   DATA_DOUBLE, temp_c,
            "temperature_fine_C", "Temperature (0.01 C)", DATA_COND, temp_raw != 0x3ff && temp_digit != 0xf, DATA_FORMAT, "%.2f C", DATA_DOUBLE, (temp_raw * 10 + temp_digit - 4000) * 0.01f,
            "humidity",         "Humidity",         DATA_COND, humidity != 0xff,    DATA_FORMAT, "%u %%",    DATA_INT, humidity,
            "temperature_raw_C", "Raw Temperature", DATA_COND, temp_raw_ext != 0x3fff, DATA_FORMAT, "%.2f C", DATA_DOUBLE, (temp_raw_ext - 4000) * 0.01f,
            "temperature_2_C",  "Temperature 2",    DATA_COND, temp_2_raw != 0x3fff, DATA_FORMAT, "%.2f C", DATA_DOUBLE, (temp_2_raw - 4000) * 0.01f,
            "pressure_hPa",     "Pressure",         DATA_COND, pressure != 0x3fff, DATA_FORMAT, "%.1f hPa", DATA_DOUBLE, pressure * 0.1f,
            "wind_dir_deg",     "Wind direction",   DATA_COND, wind_dir != 0x1ff,   DATA_INT, wind_dir,
            "wind_avg_m_s",     "Wind speed",       DATA_COND, wind_avg != 0x1ff,   DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind_avg * 0.1f,
            "wind_max_m_s",     "Gust speed",       DATA_COND, wind_max != 0x1ff,   DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wind_max * 0.1f,
            "uvi",              "UV Index",         DATA_COND, uv_index != 0xff,    DATA_FORMAT, "%.1f",     DATA_DOUBLE, uv_index * 0.1f,
            "light_lux",        "Light",            DATA_COND, light_raw != 0xffff, DATA_FORMAT, "%.1f lux", DATA_DOUBLE, (double)light_lux,
            "flags",            "Flags",            DATA_FORMAT, "%02x", DATA_INT, flags,
            "ultrasonic_status", "Ultrasonic Status", DATA_INT, ult_status,
            "ultrasonic_signal", "Ultrasonic Signal", DATA_INT, ult_signal,
            "ultrasonic_cfg",   "Ultrasonic Config", DATA_COND, ult_cfg >= 0, DATA_INT, ult_cfg,
            "rain_mm",          "Total Rain",       DATA_FORMAT, "%.1f mm", DATA_DOUBLE, rain_raw * 0.1f,
            "rain_start",       "Rain Start",       DATA_INT, rain_start,
            "rain_state",       "Rain State",       DATA_INT, rain_state,
            "rain_state_changes", "Rain State Changes", DATA_INT, rain_changes,
            "rain_intensity_sum", "Rain Intensity Sum", DATA_INT, rain_int_sum,
            "piezo_waves",      "Piezo Waves",      DATA_INT, piezo_waves,
            "piezo_rain_waves", "Piezo Rain Waves", DATA_INT, piezo_rain,
            "rain_adc_1",       "Rain ADC 1",       DATA_INT, rain_adc_1,
            "rain_adc_2",       "Rain ADC 2",       DATA_INT, rain_adc_2,
            "rain_adc_slope_1", "Rain ADC Slope 1", DATA_INT, rain_slope_1,
            "rain_adc_slope_2", "Rain ADC Slope 2", DATA_INT, rain_slope_2,
            "supercap_V",       "Supercap Voltage", DATA_COND, supercap_V != 0xff, DATA_FORMAT, "%.1f V", DATA_DOUBLE, supercap_V * 0.1f,
            "firmware",         "Firmware Version", DATA_INT, firmware,
            "data",             "Extra Data",       DATA_STRING, extra,
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
        "battery_mV",
        "temperature_C",
        "temperature_fine_C",
        "humidity",
        "temperature_raw_C",
        "temperature_2_C",
        "pressure_hPa",
        "wind_dir_deg",
        "wind_avg_m_s",
        "wind_max_m_s",
        "uvi",
        "light_lux",
        "flags",
        "ultrasonic_status",
        "ultrasonic_signal",
        "ultrasonic_cfg",
        "unknown",
        "rain_mm",
        "rain_start",
        "rain_state",
        "rain_state_changes",
        "rain_intensity_sum",
        "piezo_waves",
        "piezo_rain_waves",
        "rain_adc_1",
        "rain_adc_2",
        "rain_adc_slope_1",
        "rain_adc_slope_2",
        "supercap_V",
        "firmware",
        "data",
        "mic",
        NULL,
};

r_device const fineoffset_ws90 = {
        .name        = "Fine Offset Electronics WS90 weather station",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 58,
        .long_width  = 58,
        .reset_limit = 3000,
        .decode_fn   = &fineoffset_ws90_decode,
        .fields      = output_fields,
};
