/** @file
    AOK Electronic Limited weather station.

    Copyright (C) 2023 Bruno OCTAU (ProfBoc75) (improve integrity check for all devices here and add support for AOK-5056 weather station PR #2419)
    Copyright (C) 2023 Christian W. Zuckschwerdt <zany@triq.net> ( reverse galois and xor_shift_bytes check algorithms PR #2419)
    Copyright (C) 2019 Ryan Mounce <ryan@mounce.com.au> (PCM version)
    Copyright (C) 2018 Brad Campbell (PWM version)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */
/** @fn int holman_ws5029pcm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
AOK Electronic Limited weather station.

Known Rebrand compatible with:
- Holman iWeather Station ws5029. https://www.holmanindustries.com.au/products/iweather-station/
- Conrad Renkforce AOK-5056
- Optex Electronique 990018 SM-018 5056

Appears to be related to the Fine pos WH1080 and Digitech XC0348.

- Modulation: FSK PCM
- Frequency: 917.0 MHz +- 40 kHz
- 10 kb/s bitrate, 100 us symbol/bit time

A transmission burst is sent every 57 seconds. Each burst consists of 3
repetitions of the same "package" separated by a 1 ms gap.
The length of 196 or 218 bits depends on the device type.

Package format:
- Preamble            {48}0xAAAAAAAAAAAA
- Header              {24}0x98F3A5
- Payload             {96 or 146} see below
- zeros               {36} 0 with battery ?
- Checksum/CRC        {8}  xor 12 bytes then reverse Galois algorithm (gen = 0x00, key = 0x31) PR #2419
- Trailer/postamble   {20} direction (previous ?) and 3 zeros

Payload format: Without UV Lux sensor

    Fixed Values 0x  : AA AA AA AA AA AA 98 F3 A5

    Byte position    : 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15
    Payload          : II II CC CH HR RR WW Dx xx xx ?x xx ss 0d 00 0

- IIII        station ID (randomised on each battery insertion)
- CCC         degrees C, signed, in multiples of 0.1 C
- HH          humidity %
- RRR         cumulative rain in multiples of 0.79 mm
- WW          wind speed in km/h
- D           wind direction (0 = N, 4 = E, 8 = S, 12 = W)
- xxxxxxxxx   ???, usually zero
- ss          xor 12 bytes then reverse Galois algorithm (gen = 0x00 , key = 0x31) PR #2419

Payload format: With UV Lux sensor

    Fixed Values 0x  : AA AA AA AA AA AA 98 F3 A5

    Byte position    : 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18
    Payload          : II II CC CH HR RR WW |         | NN SS 0D 00 00 00 00 0
                                +-----------+         +-------------+
                                |                                   |
                                |   07       08       09       10   |
                  bits details : DDDDUUUU ULLLLLLL LLLLLLLL LLBBNNNN

- I     station ID (randomised on each battery insertion)
- C     degrees C, signed, in multiples of 0.1 C
- H     humidity %
- R     cumulative rain in mm
- W     wind speed in km/h
- D     wind direction (0 = N, 4 = E, 8 = S, 12 = W)
- U     Index UV
- L     Lux
- B     Battery
- N     Payload number, increase at each message 000->FFF but not always, strange behavior. no clue
- S     xor 12 bytes then reverse Galois algorithm (gen = 0x00 , key = 0x31) PR #2419
- D     Previous Wind direction
- Fixed values to 9 zeros

To get raw data
$ rtl_433 -f 917M -X 'name=AOK,modulation=FSK_PCM,short=100,long=100,preamble={48}0xAAAAAA98F3A5,reset=22000'

@sa holman_ws5029pwm_decode()

*/

#include "decoder.h"

static int holman_ws5029pcm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int const wind_dir_degr[] = {0, 23, 45, 68, 90, 113, 135, 158, 180, 203, 225, 248, 270, 293, 315, 338};
    uint8_t const preamble[] = {0xAA, 0xAA, 0xAA, 0x98, 0xF3, 0xA5};

    data_t *data;
    uint8_t b[18];

    if (bitbuffer->num_rows != 1) {
        if (decoder->verbose) {
            decoder_logf(decoder, 1, __func__, "Wrong number of rows (%d)", bitbuffer->num_rows);
        }
        return DECODE_ABORT_EARLY;
    }

    unsigned bits = bitbuffer->bits_per_row[0];

    if (bits < 192 ) {                 // too small
        return DECODE_ABORT_LENGTH;
    }

    unsigned pos = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof (preamble) * 8);

    if (pos >= bits) {
        return DECODE_ABORT_EARLY;
    }

    decoder_logf(decoder, 2, __func__, "Found AOK preamble pos: %d", pos);

    pos += sizeof(preamble) * 8;

    bitbuffer_extract_bytes(bitbuffer, 0, pos, b, sizeof(b) * 8);

    uint8_t chk_digest = b[12];
    uint8_t chk_calc = xor_bytes(b, 12);
    // reverse Galois algorithm then (gen = 0x00, key = 0x31) PR #2419
    int chk_expected = lfsr_digest8_reflect(&chk_calc, 1, 0x00, 0x31);

    if (chk_expected != chk_digest) {
        return DECODE_FAIL_MIC;
    }

    int device_id     = (b[0] << 8) | b[1];
    int temp_raw      = (int16_t)((b[2] << 8) | (b[3] & 0xf0)); // uses sign-extend
    float temp_c      = (temp_raw >> 4) * 0.1f;
    int humidity      = ((b[3] & 0x0f) << 4) | ((b[4] & 0xf0) >> 4);
    int rain_raw      = ((b[4] & 0x0f) << 8) | b[5];
    float speed_kmh   = (float)b[6];
    int direction_deg = wind_dir_degr[(b[7] & 0xf0) >> 4];

    if (bits < 200) {                 // model without UV LUX
        float rain_mm     = rain_raw * 0.79f;

        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "Holman-WS5029",
                "id",               "Station ID",        DATA_FORMAT, "%04X",       DATA_INT,    device_id,
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C",     DATA_DOUBLE, temp_c,
                "humidity",         "Humidity",         DATA_FORMAT, "%u %%",      DATA_INT,    humidity,
                "rain_mm",          "Total rainfall",   DATA_FORMAT, "%.1f mm",    DATA_DOUBLE, rain_mm,
                "wind_avg_km_h",    "Wind avg speed",   DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, speed_kmh,
                "wind_dir_deg",     "Wind Direction",   DATA_INT, direction_deg,
                "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    else if (bits < 221) {                         // model with UV LUX
        float rain_mm    = rain_raw * 1.0f;
        int uv_index     = ((b[7] & 0x07) << 1) | ((b[8] & 0x80) >> 7);
        int light_lux    = ((b[8] & 0x7F) << 10) | (b[9] << 2) | ((b[10] & 0xC0) >> 6);
        int battery_low  = ((b[10] & 0x30) >> 4);
        int counter      = ((b[10] & 0x0f) << 8 | b[11]);
        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "AOK-5056",
                "id",               "Station ID",        DATA_FORMAT, "%04X",      DATA_INT,    device_id,
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C",    DATA_DOUBLE, temp_c,
                "humidity",         "Humidity",         DATA_FORMAT, "%u %%",     DATA_INT,    humidity,
                "rain_mm",          "Total rainfall",   DATA_FORMAT, "%.1f mm",   DATA_DOUBLE, rain_mm,
                "wind_avg_km_h",    "Wind avg speed",   DATA_FORMAT, "%.1f km/h", DATA_DOUBLE, speed_kmh,
                "wind_dir_deg",     "Wind Direction",   DATA_INT,                              direction_deg,
                "uv",               "UV Index",         DATA_FORMAT, "%u",        DATA_INT,    uv_index,
                "light_lux",        "Lux",              DATA_FORMAT, "%u",        DATA_INT,    light_lux,
                "counter",          "Counter",          DATA_FORMAT, "%u",        DATA_INT,    counter,
                "battery_ok",       "battery",          DATA_FORMAT, "%u",        DATA_INT,    !battery_low,
                "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return 0;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "humidity",
        "battery_ok",
        "rain_mm",
        "wind_avg_km_h",
        "wind_dir_deg",
        "uv",
        "light_lux",
        "counter",
        "mic",
        NULL,
};

r_device const holman_ws5029pcm = {
        .name        = "AOK Weather Station rebrand Holman Industries iWeather WS5029, Conrad AOK-5056, Optex 990018",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 19200,
        .decode_fn   = &holman_ws5029pcm_decode,
        .fields      = output_fields,
};

/** @fn int holman_ws5029pwm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Holman Industries WS5029 weather station using PWM.

Package format: (invert)
- Preamble            {24} 0xAAA598
- Payload             {56} [ see below ]
- Checksum/CRC         {8} xor_shift_bytes (key = 0x18) PR #2419
- Trailer/postamble    {8} 0x00 or 0x80

Payload format:

    Byte position    : 00 01 02[03 04 05 06 07 08 09]10 11
    Payload          : AA A5 98 II BC CC HH RR RW WD SS 00

- I    station ID
- B    battery low indicator
- C    degrees C, signed, in multiples of 0.1 C
- H    Humidity 0-100 %
- R    Rain is 0.79mm / count , 618 counts / 488.2mm - 190113 - Multiplier is exactly 0.79
- W    Wind speed in km/h
- D    Wind direction, clockwise from North, in multiples of 22.5 deg
- S    xor_shift_bytes , see PR #2419

To get the raw data :
$ rtl_433 -f 433.92M -X "n=Holman-WS5029-PWM,m=FSK_PWM,s=488,l=976,g=2000,r=6000,invert"

*/

static uint8_t xor_shift_bytes(uint8_t const message[], unsigned num_bytes, uint8_t shift_up)   // see #2419 for more details about the xor_shift_bytes , used by PWM device
{
    uint8_t result0 = 0;
    for (unsigned i = 0; i < num_bytes; i += 2) {
        result0 ^= message[i];
    }
    uint8_t result1 = 0;
    for (unsigned i = 1; i < num_bytes; i += 2) {
        result1 ^= message[i];
    }
    uint8_t resultx = 0;
    for (unsigned j = 0; j < 7; ++j) {
        if (shift_up & (1 << j))
            resultx ^= result0 << (j + 1);
    }
    return result0 ^ result1 ^ resultx;
}

static int holman_ws5029pwm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x55, 0x5a, 0x67}; // Preamble/Device inverted

    data_t *data;
    uint8_t *b;
    uint16_t temp_raw;
    int id, humidity, wind_dir, battery_low;
    float temp_c, rain_mm, speed_kmh;

    // Data is inverted, but all these checks can be performed
    // and validated prior to inverting the buffer. Invert
    // only if we have a valid row to process.
    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 96);
    if (r < 0 || bitbuffer->bits_per_row[r] != 96)
        return DECODE_ABORT_LENGTH;

    b = bitbuffer->bb[r];

    // Test for preamble / device code
    if (memcmp(b, preamble, 3))
        return DECODE_FAIL_SANITY;

    // Invert data for processing
    bitbuffer_invert(bitbuffer);

    uint8_t chk_digest = b[10];
    // xor_shift_bytes , see PR #2419
    int chk_calc = xor_shift_bytes(b, 10, 0x18);
    //fprintf(stderr, "%s: 11th byte %02x chk_calc %02x \n", __func__, chk_digest, chk_calc );

    if (chk_calc != chk_digest) {
        return DECODE_FAIL_MIC;
    }

    id          = b[3];                                                // changes on each power cycle
    battery_low = (b[4] & 0x80);                                       // High bit is low battery indicator
    temp_raw    = (int16_t)(((b[4] & 0x0f) << 12) | (b[5] << 4));      // uses sign-extend
    temp_c      = (temp_raw >> 4) * 0.1f;                              // Convert sign extended int to float
    humidity    = b[6];                                                // Simple 0-100 RH
    rain_mm     = ((b[7] << 4) + (b[8] >> 4)) * 0.79f;                 // Multiplier tested empirically over 618 pulses
    speed_kmh   = (float)(((b[8] & 0xF) << 4) + (b[9] >> 4));          // In discrete kph
    wind_dir    = b[9] & 0xF;                                          // 4 bit wind direction, clockwise from North

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "Holman-WS5029",
            "id",               "",                 DATA_INT,    id,
            "battery_ok",       "Battery",          DATA_INT,    !battery_low,
            "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C",     DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",         DATA_FORMAT, "%u %%",       DATA_INT,    humidity,
            "rain_mm",          "Total rainfall",   DATA_FORMAT, "%.1f mm",    DATA_DOUBLE, rain_mm,
            "wind_avg_km_h",    "Wind avg speed",   DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, speed_kmh,
            "wind_dir_deg",     "Wind Direction",   DATA_INT,    (int)(wind_dir * 22.5),
            "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

r_device const holman_ws5029pwm = {
        .name        = "Holman Industries iWeather WS5029 weather station (older PWM)",
        .modulation  = FSK_PULSE_PWM,
        .short_width = 488,
        .long_width  = 976,
        .reset_limit = 6000,
        .gap_limit   = 2000,
        .decode_fn   = &holman_ws5029pwm_decode,
        .fields      = output_fields,
};
