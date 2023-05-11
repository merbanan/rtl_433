/** @file
    Fine Offset WH1050 Weather Station.

    2016 Nicola Quiriti ('ovrheat')
    Modifications 2016 by Don More

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 */

#include "decoder.h"

/**
Fine Offset WH1050 Weather Station.

This module is a cut-down version of the WH1080 decoder.
The WH1050 sensor unit is like the WH1080 unit except it has no
wind direction sensor or time receiver.
Other than omitting the unused code, the differences are the message length
and the location of the battery-low bit.

This weather station is based on an indoor touchscreen receiver, and on a 5+1 outdoor wireless sensors group
(rain, wind speed, temperature, humidity.
See the product page here: http://www.foshk.com/Weather_Professional/WH1070.html (The 1050 model has no radio clock).

Please note that the pressure sensor (barometer) is enclosed in the indoor console unit, NOT in the outdoor
wireless sensors group.
That's why it's NOT possible to get pressure data by wireless communication. If you need pressure data you should try
an Arduino/Raspberry solution wired with a BMP180 or BMP085 sensor.

Data is transmitted every 48 seconds, alternating between sending a single packet and sending two packets in quick succession
(almost always identical, but clearly generated separately because during e.g. heavy rainfall different values have been observed).
I.e., data packet, wait 48 seconds, two data packets, wait 48 seconds, data packet, wait 48 seconds, two data packets, ... .

The 'Total rainfall' field is a cumulative counter, increased by 0.3 millimeters of rain each step.

The station is also known as TFA STRATOS 35.1077
See the product page here: https://www.tfa-dostmann.de/en/product/wireless-weather-station-with-wind-and-rain-gauge-stratos-35-1077/
This model seems also capable to decode the DCF77 time signal sent by the time signal decoder (which is enclosed on the sensor tx):
around the minute 59 of the even hours the sensor's TX stops sending weather data, probably to receive (and sync with) DCF77 signals.
After around 3-4 minutes of silence it starts to send just time data for some minute, then it starts again with
weather data as usual.

To recognize which message is received (weather or time) you can use the 'msg_type' field on json output:
- msg_type 0 = weather data
- msg_type 1 = time data

Weather data - Message layout and example:

         AA BC CD DD EE FF GG HH HH II
    {80} ff 5f 51 93 48 00 00 12 46 aa

- A :  8 bits : Preamble 0xFF
- B :  4 bits : ?? - seems to be 0x5 for whether data, 0x6 for time data
- C :  8 bits : Id, changes when reset (e.g., 0xF5)
- D :  1 bit  : msg_type - 0, for whether data
- D :  1 bit  : Battery, 0 = ok, 1 = low (e.g, OK)
- D : 10 bits : Temperature in Celsius, offset 400, scaled by 10 (e.g., 0.3 degrees C)
- E :  8 bits : Relative humidity, percent (e.g., 72%)
- F :  8 bits : Wind speed average in m/s, scaled by 1/0.34 (e.g., 0 m/s)
- G :  8 bits : Wind speed gust in m/s, scaled by 1/0.34 (e.g., 0 m/s)
- H : 16 bits : Total rainfall in units of 0.3mm, since reset (e.g., 1403.4 mm)
- I :  8 bits : CRC, poly 0x31, init 0x00 (excluding preamble)

Time data - Message layout and example:

        AA BC CD DE FG HI JK LM NO PP
   {80} ff 69 0a 96 02 41 23 43 27 df

- A :  8 bits : Preamble 0xFF
- B :  4 bits : ?? - seems to be 0x5 for whether data, seems 0x6 for time data
- C :  8 bits : Id, changes when reset (e.g., 0xF5)
- D :  1 bit  : msg_type - 1, for time data
- D :  1 bit  : Battery, 0 = ok, 1 = low (e.g, OK)
- D :  4 bits : ??
- D :  2 bits : hour BCD coded (*10)
- E :  4 bits : hour BCD coded (*1)
- F :  4 bits : minute BCD coded (*10)
- G :  4 bits : minute BCD coded (*1)
- H :  4 bits : second BCD coded (*10)
- I :  4 bits : second BCD coded (*1)
- J :  4 bits : year BCD coded (*10), counted from 2000
- K :  4 bits : year BCD coded (*1), counted from 2000
- L :  3 bits : ??
- L :  1 bits : month BCD coded (*10)
- M :  4 bits : month BCD coded (*1)
- N :  4 bits : day BCD coded (*10)
- O :  4 bits : day BCD coded (*1)
- P :  8 bits : CRC, poly 0x31, init 0x00 (excluding preamble)

*/
static int fineoffset_wh1050_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t br[9];

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    /* The normal preamble for WH1050 is 8 1s (0xFF) followed by 4 0s
       for a total 80 bit message.
       (The 4 0s is not confirmed to be preamble but seems to be zero on most devices)

       Digitech XC0346 (and possibly other models) only sends 7 1 bits not 8 (0xFE)
       for some reason (maybe transmitter module is slow to wake up), for a total
       79 bit message.

       In both cases, we extract the 72 bits after the preamble.
    */
    unsigned bits = bitbuffer->bits_per_row[0];
    uint8_t preamble_byte = bitbuffer->bb[0][0];
    if (bits == 79 && preamble_byte == 0xfe) {
        bitbuffer_extract_bytes(bitbuffer, 0, 7, br, 72);
    } else if (bits == 80 && preamble_byte == 0xff) {
        bitbuffer_extract_bytes(bitbuffer, 0, 8, br, 72);
    } else {
        return DECODE_ABORT_LENGTH;
    }

    // If you calculate the CRC over all 10 bytes including the preamble
    // byte (always 0xFF), then CRC_INIT is 0xFF. But we compare the preamble
    // byte and then discard it.
    if (crc8(br, 9, 0x31, 0x00)) {
        return DECODE_FAIL_MIC; // crc mismatch
    }

    // GETTING MESSAGE TYPE
    int msg_type = (br[1] & 0x08) >> 3;

    if (msg_type == 0) {
        // GETTING WEATHER SENSORS DATA
        int temp_raw      = ((br[1] & 0x03) << 8) | br[2];
        float temperature = (temp_raw - 400) * 0.1f;
        int humidity      = br[3];
        float speed       = (br[4] * 0.34f) * 3.6f; // m/s -> km/h
        float gust        = (br[5] * 0.34f) * 3.6f; // m/s -> km/h
        int rain_raw      = (br[6] << 8) | br[7];
        float rain        = rain_raw * 0.3f;
        int device_id     = (br[0] << 4 & 0xf0) | (br[1] >> 4);
        int battery_low   = br[1] & 0x04;

        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "Fineoffset-WH1050",
                "id",               "Station ID",        DATA_FORMAT, "%04X",    DATA_INT,    device_id,
                "msg_type",         "Msg type",         DATA_INT,    msg_type,
                "battery_ok",       "Battery",          DATA_INT,    !battery_low,
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
                "humidity",         "Humidity",         DATA_FORMAT, "%u %%",   DATA_INT,    humidity,
                "wind_avg_km_h",    "Wind avg speed",   DATA_FORMAT, "%.02f",   DATA_DOUBLE, speed,
                "wind_max_km_h",    "Wind gust",        DATA_FORMAT, "%.02f",   DATA_DOUBLE, gust,
                "rain_mm",          "Total rainfall",   DATA_FORMAT, "%.01f",   DATA_DOUBLE, rain,
                "mic",              "Integrity",        DATA_STRING, "CRC",
                NULL);
        /* clang-format on */
    }
    else {
        // GETTING TIME DATA
        int device_id   = (br[0] << 4 & 0xf0) | (br[1] >> 4);
        int battery_low = br[1] & 0x04;
        int hours       = ((br[2] & 0x30) >> 4) * 10 + (br[2] & 0x0F);
        int minutes     = ((br[3] & 0xF0) >> 4) * 10 + (br[3] & 0x0F);
        int seconds     = ((br[4] & 0xF0) >> 4) * 10 + (br[4] & 0x0F);
        int year        = ((br[5] & 0xF0) >> 4) * 10 + (br[5] & 0x0F) + 2000;
        int month       = ((br[6] & 0x10) >> 4) * 10 + (br[6] & 0x0F);
        int day         = ((br[7] & 0xF0) >> 4) * 10 + (br[7] & 0x0F);

        char clock_str[23];
        sprintf(clock_str, "%04d-%02d-%02dT%02d:%02d:%02d",
                year, month, day, hours, minutes, seconds);

        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING,    "Fineoffset-WH1050",
                "id",               "Station ID",       DATA_INT,       device_id,
                "msg_type",         "Msg type",         DATA_INT,       msg_type,
                "battery_ok",       "Battery",          DATA_INT,       !battery_low,
                "radio_clock",      "Radio Clock",      DATA_STRING,    clock_str,
                "mic",              "Integrity",        DATA_STRING,    "CRC",
                NULL);
        /* clang-format on */
    }

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "msg_type",
        "battery_ok",
        "temperature_C",
        "humidity",
        "wind_avg_km_h",
        "wind_max_km_h",
        "rain_mm",
        "radio_clock",
        "mic",
        NULL,
};

r_device const fineoffset_wh1050 = {
        .name        = "Fine Offset WH1050 Weather Station",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 544,
        .long_width  = 1524,
        .reset_limit = 10520,
        .decode_fn   = &fineoffset_wh1050_decode,
        .fields      = output_fields,
};
