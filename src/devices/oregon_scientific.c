/** @file
    Various Oregon Scientific protocols.

    Copyright (C) 2015 Helge Weissig, Denis Bodor, Tommy Vestermark, Karl Lattimer,
    deennoo, pclov3r, onlinux, Pasquale Fiorillo.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

// Documentation for Oregon Scientific protocols can be found here:
// http://wmrx00.sourceforge.net/Arduino/OregonScientific-RF-Protocols.pdf
//
// note that at least for THN132N, THGR122N, THGR810 valid channel numbers are 1, 2, 4.
// Sensors ID
#define ID_THGR122N 0x1d20
#define ID_THGR968  0x1d30
#define ID_BTHR918  0x5d50
#define ID_BHTR968  0x5d60
#define ID_RGR968   0x2d10
#define ID_THR228N  0xec40
#define ID_THN132N  0xec40 // same as THR228N but different packet size
#define ID_AWR129   0xec41 // similar to THR228N, but an extra 100s digit
#define ID_RTGN318  0x0cc3 // warning: id is from 0x0cc3 and 0xfcc3
#define ID_RTGN129  0x0cc3 // same as RTGN318 but different packet size
#define ID_THGR810  0xf824 // This might be ID_THGR81, but what's true is lost in (git) history
#define ID_THGR810a 0xf8b4 // unconfirmed version
#define ID_THN802   0xc844
#define ID_PCR800   0x2914
#define ID_PCR800a  0x2d14 // Different PCR800 ID - AU version I think
#define ID_WGR800   0x1984
#define ID_WGR800a  0x1994 // unconfirmed version
#define ID_WGR968   0x3d00
#define ID_UV800    0xd874
#define ID_THN129   0xcc43 // THN129 Temp only
#define ID_RTHN129  0x0cd3 // RTHN129 Temp, clock sensors
#define ID_BTHGN129 0x5d53 // Baro, Temp, Hygro sensor
#define ID_UVR128   0xec70
#define ID_THGR328N   0xcc23  // Temp & Hygro sensor looks similar to THR228N but with 5 choice channel instead of 3
#define ID_RTGR328N_1 0xdcc3  // RTGR328N_[1-5] RFclock (date &time) & Temp & Hygro sensor looks similar to THGR328N with RF clock (5 channels also) : Temp & hygro part
#define ID_RTGR328N_2 0xccc3
#define ID_RTGR328N_3 0xbcc3
#define ID_RTGR328N_4 0xacc3
#define ID_RTGR328N_5 0x9cc3
#define ID_RTGR328N_6 0x8ce3  // RTGR328N_6&7 RFclock (date &time) & Temp & Hygro sensor looks similar to THGR328N with RF clock (5 channels also) : RF Time part
#define ID_RTGR328N_7 0x8ae3

static float get_os_temperature(unsigned char *message)
{
    float temp_c = 0;
    temp_c = (((message[5] >> 4) * 100) + ((message[4] & 0x0f) * 10) + ((message[4] >> 4) & 0x0f)) / 10.0F;
    // The AWR129 BBQ thermometer has another digit to represent higher temperatures than what weather stations would observe.
    temp_c += (message[5] & 0x07) * 100.0F;
    // 0x08 is the sign bit
    if (message[5] & 0x08) {
        temp_c = -temp_c;
    }
    return temp_c;
}

static float get_os_rain_rate(unsigned char *message)
{
    // Nibbles 11..8 rain rate, LSD = 0.1 units per hour, 4321 = 123.4 units per hour
    float rain_rate = (((message[5] & 0x0f) * 1000) + ((message[5] >> 4) * 100) + ((message[4] & 0x0f) * 10) + (message[4] >> 4)) / 100.0F;
    return rain_rate;
}

static float get_os_total_rain(unsigned char *message)
{
    float total_rain = 0.0F; // Nibbles 17..12 Total rain, LSD = 0.001, 654321 = 123.456
    total_rain = (message[8] & 0x0f) * 100.0F
            + ((message[8] >> 4) & 0x0f) * 10.0F + (message[7] & 0x0f)
            + ((message[7] >> 4) & 0x0f) / 10.0F + (message[6] & 0x0f) / 100.0F
            + ((message[6] >> 4) & 0x0f) / 1000.0F;
    return total_rain;
}

static unsigned int get_os_humidity(unsigned char *message)
{
    int humidity = 0;
    humidity = ((message[6] & 0x0f) * 10) + (message[6] >> 4);
    return humidity;
}

static unsigned int get_os_uv(unsigned char *message)
{
    int uvidx = 0;
    uvidx = ((message[4] & 0x0f) * 10) + (message[4] >> 4);
    return uvidx;
}

static unsigned cm180i_power(uint8_t const *msg, unsigned int offset)
{
    unsigned val = 0;

    val = (msg[4+offset*2] << 8) | (msg[3+offset*2] & 0xF0);
    // tested across situations varying from 700 watt to more than 8000 watt to
    // get same value as showed in physical CM180 panel (exactly equals to 1+1/160)
    val *= 1.00625;
    return val;
}

static uint64_t cm180i_total(uint8_t const *msg)
{
    uint64_t val = 0;
    if ((msg[1] & 0x0F) == 0) {
        // Sensor returns total only if nibble#4 == 0
        val = (uint64_t)msg[14] << 40;
        val += (uint64_t)msg[13] << 32;
        val += (uint32_t)msg[12] << 24;
        val += msg[11] << 16;
        val += msg[10] << 8;
        val += msg[9];
    }
    return val;
}

static uint8_t swap_nibbles(uint8_t byte)
{
    return (((byte&0xf) << 4) | (byte >> 4));
}

static unsigned cm180_power(uint8_t const *msg)
{
    unsigned val = 0;
    val = (msg[4] << 8) | (msg[3] & 0xF0);
    // tested across situations varying from 700 watt to more than 8000 watt to
    // get same value as showed in physical CM180 panel (exactly equals to 1+1/160)
    val *= 1.00625;
    return val;
}

static uint64_t cm180_total(uint8_t const *msg)
{
    uint64_t val = 0;
    if ((msg[1] & 0x0F) == 0) {
        // Sensor returns total only if nibble#4 == 0
        val = (uint64_t)msg[10] << 40;
        val += (uint64_t)msg[9] << 32;
        val += (uint32_t)msg[8] << 24;
        val += msg[7] << 16;
        val += msg[6] << 8;
        val += msg[5];
    }
    return val;
}

static int validate_os_checksum(r_device *decoder, unsigned char *msg, int checksum_nibble_idx)
{
    // Oregon Scientific v2.1 and v3 checksum is a    1 byte    'sum of nibbles' checksum.
    // with the 2 nibbles of the checksum byte    swapped.
    int i;
    unsigned int checksum, sum_of_nibbles = 0;
    for (i = 0; i < checksum_nibble_idx - 1; i += 2) {
        unsigned char val = msg[i >> 1];
        sum_of_nibbles += ((val >> 4) + (val & 0x0f));
    }
    if (checksum_nibble_idx & 1) {
        sum_of_nibbles += (msg[checksum_nibble_idx >> 1] >> 4);
        checksum = (msg[checksum_nibble_idx >> 1] & 0x0f) | (msg[(checksum_nibble_idx + 1) >> 1] & 0xf0);
    }
    else {
        checksum = (msg[checksum_nibble_idx >> 1] >> 4) | ((msg[checksum_nibble_idx >> 1] & 0x0f) << 4);
    }
    sum_of_nibbles &= 0xff;

    if (sum_of_nibbles == checksum) {
        return 0;
    }
    else {
        decoder_logf(decoder, 1, __func__, "Checksum error in Oregon Scientific message.    Expected: %02x    Calculated: %02x", checksum, sum_of_nibbles);
        decoder_log_bitrow(decoder, 1, __func__, msg, ((checksum_nibble_idx + 4) >> 1) * 8, "Message");
        return 1;
    }
}

static int validate_os_v2_message(r_device *decoder, unsigned char *msg, int bits_expected, int msg_bits,
        int nibbles_in_checksum)
{
    // Compare number of valid bits processed vs number expected
    if (bits_expected == msg_bits) {
        return validate_os_checksum(decoder, msg, nibbles_in_checksum);
    }
    decoder_logf_bitrow(decoder, 1, __func__, msg, msg_bits, "Bit validation error on Oregon Scientific message. Expected %d bits, Message", bits_expected);
    return 1;
}

/**
Various Oregon Scientific protocols.

@todo Documentation needed.
*/
static int oregon_scientific_v2_1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b = bitbuffer->bb[0];
    data_t *data;

    // Check 2nd and 3rd bytes of stream for possible Oregon Scientific v2.1 sensor data (skip first byte to get past sync/startup bit errors)
    if (((b[1] != 0x55) || (b[2] != 0x55))
            && ((b[1] != 0xAA) || (b[2] != 0xAA))) {
        if (b[3] != 0) {
            decoder_log_bitrow(decoder, 1, __func__, b, bitbuffer->bits_per_row[0], "Badly formatted OS v2.1 message");
        }
        return DECODE_ABORT_EARLY;
    }

    bitbuffer_t databits = {0};
    uint8_t *msg = databits.bb[0];

    // Possible    v2.1 Protocol message
    unsigned int sync_test_val = ((unsigned)b[3] << 24) | (b[4] << 16) | (b[5] << 8) | (b[6]);
    // Could be extra/dropped bits in stream.    Look for sync byte at expected position +/- some bits in either direction
    for (int pattern_index = 0; pattern_index < 8; pattern_index++) {
        unsigned int mask     = (unsigned int)(0xffff0000 >> pattern_index);
        unsigned int pattern  = (unsigned int)(0x55990000 >> pattern_index);
        unsigned int pattern2 = (unsigned int)(0xaa990000 >> pattern_index);

        decoder_logf(decoder, 1, __func__, "OS v2.1 sync byte search - test_val=%08x pattern=%08x    mask=%08x", sync_test_val, pattern, mask);

        if (((sync_test_val & mask) != pattern)
                && ((sync_test_val & mask) != pattern2))
            continue; // DECODE_ABORT_EARLY

        // Found sync byte - start working on decoding the stream data.
        // pattern_index indicates    where sync nibble starts, so now we can find the start of the payload
        decoder_logf(decoder, 1, __func__, "OS v2.1 Sync test val %08x found, starting decode at bit %d", sync_test_val, pattern_index);

        //decoder_log_bitrow(decoder, 0, __func__, b, bitbuffer->bits_per_row[0], "Raw OSv2 bits");
        bitbuffer_manchester_decode(bitbuffer, 0, pattern_index + 40, &databits, 173);
        reflect_nibbles(databits.bb[0], (databits.bits_per_row[0]+7)/8);
        //decoder_logf_bitbuffer(decoder, 0, __func__, &databits, "MC OSv2 bits (from %d+40)", pattern_index);

        break;
    }
    int msg_bits = databits.bits_per_row[0];

    int sensor_id   = (msg[0] << 8) | msg[1];
    int channel     = (msg[2] >> 4) & 0x0f;
    int device_id   = (msg[2] & 0x0f) | (msg[3] & 0xf0);
    int battery_low = (msg[3] >> 2) & 0x01;

    decoder_logf(decoder, 1, __func__,"Found sensor type (%08x)", sensor_id);
    if ((sensor_id == ID_THGR122N) || (sensor_id == ID_THGR968)) {
        if (validate_os_v2_message(decoder, msg, 76, msg_bits, 15) != 0)
            return 0;
        /* clang-format off */
        data = data_make(
                "model",                 "",                        DATA_STRING, (sensor_id == ID_THGR122N) ? "Oregon-THGR122N" : "Oregon-THGR968",
                "id",                        "House Code",    DATA_INT,        device_id,
                "channel",             "Channel",         DATA_INT,        channel,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, get_os_temperature(msg),
                "humidity",            "Humidity",        DATA_FORMAT, "%u %%",     DATA_INT,        get_os_humidity(msg),
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_WGR968) {
        if (validate_os_v2_message(decoder, msg, 94, msg_bits, 17) != 0)
            return 0;
        float quadrant      = (msg[4] & 0x0f) * 10 + ((msg[4] >> 4) & 0x0f) * 1 + ((msg[5] >> 4) & 0x0f) * 100;
        float avgWindspeed  = ((msg[7] >> 4) & 0x0f) / 10.0F + (msg[7] & 0x0f) * 1.0F + ((msg[8] >> 4) & 0x0f) / 10.0F;
        float gustWindspeed = (msg[5] & 0x0f) / 10.0F + ((msg[6] >> 4) & 0x0f) * 1.0F + (msg[6] & 0x0f) / 10.0F;
        /* clang-format off */
        data = data_make(
                "model",            "",                     DATA_STRING, "Oregon-WGR968",
                "id",                 "House Code", DATA_INT,        device_id,
                "channel",        "Channel",        DATA_INT,        channel,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "wind_max_m_s", "Gust",             DATA_FORMAT, "%2.1f m/s",DATA_DOUBLE, gustWindspeed,
                "wind_avg_m_s", "Average",        DATA_FORMAT, "%2.1f m/s",DATA_DOUBLE, avgWindspeed,
                "wind_dir_deg",    "Direction",    DATA_FORMAT, "%3.1f degrees",DATA_DOUBLE, quadrant,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_BHTR968) {
        if (validate_os_v2_message(decoder, msg, 92, msg_bits, 19) != 0)
            return 0;
        //unsigned int comfort = msg[7] >> 4;
        //char *comfort_str = "Normal";
        //if (comfort == 4) comfort_str = "Comfortable";
        //else if (comfort == 8) comfort_str = "Dry";
        //else if (comfort == 0xc) comfort_str = "Humid";
        //unsigned int forecast = msg[9] >> 4;
        //char *forecast_str = "Cloudy";
        //if (forecast == 3) forecast_str = "Rainy";
        //else if (forecast == 6) forecast_str = "Partly Cloudy";
        //else if (forecast == 0xc) forecast_str = "Sunny";
        float temp_c = get_os_temperature(msg);
        float pressure = ((msg[7] & 0x0f) | (msg[8] & 0xf0)) + 856;
        // decoder_logf(decoder, 0, __func__,"Weather Sensor BHTR968    Indoor        Temp: %3.1fC    %3.1fF     Humidity: %d%%", temp_c, ((temp_c*9)/5)+32, get_os_humidity(msg));
        // decoder_logf(decoder, 0, __func__, " (%s) Pressure: %dmbar (%s)", comfort_str, ((msg[7] & 0x0f) | (msg[8] & 0xf0))+856, forecast_str);
        /* clang-format off */
        data = data_make(
                "model",            "",                             DATA_STRING, "Oregon-BHTR968",
                "id",                 "House Code",         DATA_INT,        device_id,
                "channel",        "Channel",                DATA_INT,        channel,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                "humidity",     "Humidity",             DATA_FORMAT, "%u %%",     DATA_INT,        get_os_humidity(msg),
                "pressure_hPa",    "Pressure",        DATA_FORMAT, "%.0f hPa",     DATA_DOUBLE, pressure,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_BTHR918) {
        // Similar to the BHTR968, but smaller message and slightly different pressure offset
        if (validate_os_v2_message(decoder, msg, 84, msg_bits, 19) != 0)
            return 0;
        float temp_c = get_os_temperature(msg);
        float pressure = ((msg[7] & 0x0f) | (msg[8] & 0xf0)) + 795;
        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING,    "Oregon-BTHR918",
                "id",               "House Code",       DATA_INT,       device_id,
                "channel",          "Channel",          DATA_INT,       channel,
                "battery_ok",       "Battery",          DATA_INT,       !battery_low,
                "temperature_C",    "Celsius",          DATA_FORMAT,    "%.02f C", DATA_DOUBLE, temp_c,
                "humidity",         "Humidity",         DATA_FORMAT,    "%u %%", DATA_INT, get_os_humidity(msg),
                "pressure_hPa",     "Pressure",         DATA_FORMAT,    "%.0f hPa", DATA_DOUBLE, pressure,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_RGR968) {
        if (validate_os_v2_message(decoder, msg, 80, msg_bits, 16) != 0)
            return 0;
        float rain_rate  = ((msg[4] & 0x0f) * 100 + (msg[4] >> 4) * 10 + ((msg[5] >> 4) & 0x0f)) / 10.0F;
        float total_rain = ((msg[7] & 0xf) * 10000 + (msg[7] >> 4) * 1000 + (msg[6] & 0xf) * 100 + (msg[6] >> 4) * 10 + (msg[5] & 0xf)) / 10.0F;
        /* clang-format off */
        data = data_make(
                "model",            "",                     DATA_STRING, "Oregon-RGR968",
                "id",                 "House Code", DATA_INT,        device_id,
                "channel",        "Channel",        DATA_INT,        channel,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "rain_rate_mm_h",    "Rain Rate",    DATA_FORMAT, "%.02f mm/h", DATA_DOUBLE, rain_rate,
                "rain_mm", "Total Rain", DATA_FORMAT, "%.02f mm", DATA_DOUBLE, total_rain,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if ((sensor_id == ID_THR228N || sensor_id == ID_AWR129) && msg_bits == 76) {
        if (validate_os_v2_message(decoder, msg, 76, msg_bits, 12) != 0)
            return 0;
        float temp_c = get_os_temperature(msg);
        /* clang-format off */
        data = data_make(
                "model", "", DATA_COND, sensor_id == ID_THR228N, DATA_STRING, "Oregon-THR228N",
                "model", "", DATA_COND, sensor_id == ID_AWR129, DATA_STRING, "Oregon-AWR129",
                "id",                        "House Code",    DATA_INT,        device_id,
                "channel",             "Channel",         DATA_INT,        channel,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_THN132N && msg_bits == 64) {
        if (validate_os_v2_message(decoder, msg, 64, msg_bits, 12) != 0)
            return 0;
        // Sanity check BCD digits
        if (((msg[5] >> 4) & 0x0F) > 9 || (msg[4] & 0x0F) > 9 || ((msg[4] >> 4) & 0x0F) > 9) {
            decoder_log(decoder, 1, __func__, "THN132N Message failed BCD sanity check.");
            return DECODE_FAIL_SANITY;
        }
        float temp_c = get_os_temperature(msg);
        // Sanity check value
        if (temp_c > 70 || temp_c < -50) {
            decoder_logf(decoder, 1, __func__, "THN132N Message failed values sanity check: temperature_C %3.1fC.", temp_c);
            return DECODE_FAIL_SANITY;
        }

        /* clang-format off */
        data = data_make(
                "model",                 "",                        DATA_STRING, "Oregon-THN132N",
                "id",                        "House Code",    DATA_INT,        device_id,
                "channel",             "Channel",         DATA_INT,        channel,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if ((sensor_id & 0x0fff) == ID_RTGN129 && msg_bits == 80) {
        if (validate_os_v2_message(decoder, msg, 80, msg_bits, 15) != 0)
            return 0;
        float temp_c = get_os_temperature(msg);
        /* clang-format off */
        data = data_make(
                "model",                 "",                        DATA_STRING, "Oregon-RTGN129",
                "id",                        "House Code",    DATA_INT,        device_id,
                "channel",             "Channel",         DATA_INT,        channel, // 1 to 5
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                "humidity",            "Humidity",        DATA_FORMAT, "%u %%",     DATA_INT,        get_os_humidity(msg),
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (((sensor_id == ID_RTGR328N_1) || (sensor_id == ID_RTGR328N_2) || (sensor_id == ID_RTGR328N_3) || (sensor_id == ID_RTGR328N_4) || (sensor_id == ID_RTGR328N_5)) && msg_bits == 173) {
        if (validate_os_v2_message(decoder, msg, 173, msg_bits, 15) != 0)
             return 0;
        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Oregon-RTGR328N",
                "id",               "House Code",   DATA_INT,    device_id,
                "channel",          "Channel",      DATA_INT,    channel, // 1 to 5
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.02f C", DATA_DOUBLE, get_os_temperature(msg),
                "humidity",         "Humidity",     DATA_FORMAT, "%u %%",   DATA_INT,    get_os_humidity(msg),
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if ((sensor_id == ID_RTGR328N_6) || (sensor_id == ID_RTGR328N_7)) {
        if (validate_os_v2_message(decoder, msg, 100, msg_bits, 21) != 0)
            return 0;

        int year    = ((msg[9] & 0x0F) * 10) + ((msg[9] & 0xF0) >> 4) + 2000;
        int month   = ((msg[8] & 0xF0) >> 4);
        //int weekday = ((msg[8] & 0x0F));
        int day     = ((msg[7] & 0x0F) * 10) + ((msg[7] & 0xF0) >> 4);
        int hours   = ((msg[6] & 0x0F) * 10) + ((msg[6] & 0xF0) >> 4);
        int minutes = ((msg[5] & 0x0F) * 10) + ((msg[5] & 0xF0) >> 4);
        int seconds = ((msg[4] & 0x0F) * 10) + ((msg[4] & 0xF0) >> 4);

        char clock_str[24];
        snprintf(clock_str, sizeof(clock_str), "%04d-%02d-%02dT%02d:%02d:%02d",
                year, month, day, hours, minutes, seconds);

        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Oregon-RTGR328N",
                "id",               "House Code",   DATA_INT,    device_id,
                "channel",          "Channel",      DATA_INT,    channel, // 1 to 5
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "radio_clock",      "Radio Clock",  DATA_STRING, clock_str,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if ((sensor_id & 0x0fff) == ID_RTGN318) {
        if (msg_bits == 76 && (validate_os_v2_message(decoder, msg, 76, msg_bits, 15) == 0)) {
            float temp_c = get_os_temperature(msg);
            /* clang-format off */
            data = data_make(
                    "model",                 "",                        DATA_STRING, "Oregon-RTGN318",
                    "id",                        "House Code",    DATA_INT,        device_id,
                    "channel",             "Channel",         DATA_INT,        channel, // 1 to 5
                    "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                    "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                    "humidity",            "Humidity",        DATA_FORMAT, "%u %%",     DATA_INT,        get_os_humidity(msg),
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            return 1;
        }
        else if (msg_bits == 100 && (validate_os_v2_message(decoder, msg, 100, msg_bits, 21) == 0)) {
            // RF Clock message ??
            return 0;
        }
    }
    else if (sensor_id == ID_THN129 || (sensor_id & 0x0FFF) == ID_RTHN129) {
        if ((validate_os_v2_message(decoder, msg, 68, msg_bits, 12) == 0)) {
            float temp_c = get_os_temperature(msg);
            /* clang-format off */
            data = data_make(
                    "model",                 "",                        DATA_STRING, (sensor_id == ID_THN129) ? "Oregon-THN129" : "Oregon-RTHN129",
                    "id",                        "House Code",    DATA_INT,        device_id,
                    "channel",             "Channel",         DATA_INT,        channel, // 1 to 5
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                    "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            return 1;
        }
        else if (msg_bits == 104 && (validate_os_v2_message(decoder, msg, 104, msg_bits, 18) == 0)) {
            // RF Clock message
            return 0;
        }
    }
    else if (sensor_id == ID_BTHGN129) {
        if (validate_os_v2_message(decoder, msg, 92, msg_bits, 19) != 0)
            return 0;
        float temp_c = get_os_temperature(msg);
        // Pressure is given in hPa. You may need to adjust the offset
        // according to your altitude level (600 is a good starting point)
        float pressure = ((msg[7] & 0x0f) | (msg[8] & 0xf0)) * 2 + (msg[8] & 0x01) + 600;
        /* clang-format off */
        data = data_make(
                "model",                 "",                        DATA_STRING, "Oregon-BTHGN129",
                "id",                        "House Code",    DATA_INT,        device_id,
                "channel",             "Channel",         DATA_INT,        channel, // 1 to 5
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                "humidity",             "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, get_os_humidity(msg),
                "pressure_hPa",    "Pressure",        DATA_FORMAT, "%.02f hPa", DATA_DOUBLE, pressure,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_UVR128 && msg_bits == 148) {
        if (validate_os_v2_message(decoder, msg, 148, msg_bits, 12) != 0)
            return 0;
        // Sanity check BCD digits
        if (((msg[4] >> 4) & 0x0F) > 9 || (msg[4] & 0x0F) > 9) {
            decoder_log(decoder, 1, __func__, "UVR128 Message failed BCD sanity check.");
            return DECODE_FAIL_SANITY;
        }
        int uvidx = get_os_uv(msg);
        // Sanity check value
        if (uvidx < 0 || uvidx > 25) {
            decoder_logf(decoder, 1, __func__, "UVR128 Message failed values sanity check: uv %u.", uvidx);
            return DECODE_FAIL_SANITY;
        }

        /* clang-format off */
        data = data_make(
                "model",                    "",                     DATA_STRING, "Oregon-UVR128",
                "id",                         "House Code", DATA_INT,        device_id,
                "uv",                         "UV Index",     DATA_FORMAT, "%u", DATA_INT, uvidx,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                //"channel",                "Channel",        DATA_INT,        channel,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_THGR328N) {
        if (validate_os_v2_message(decoder, msg, 173, msg_bits, 15) != 0)
            return 0;
        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Oregon-THGR328N",
                "id",               "House Code",   DATA_INT,    device_id,
                "channel",          "Channel",      DATA_INT,    channel, // 1 to 5
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.02f C", DATA_DOUBLE, get_os_temperature(msg),
                "humidity",         "Humidity",     DATA_FORMAT, "%u %%",   DATA_INT,    get_os_humidity(msg),
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (msg_bits > 16) {
        decoder_logf_bitrow(decoder, 1, __func__, msg, msg_bits, "Unrecognized Oregon Scientific v2.1 message (sensor type %04x)", sensor_id);
    }
    else {
        decoder_log_bitrow(decoder, 1, __func__, b, bitbuffer->bits_per_row[0], "Possible Oregon Scientific v2.1 message, but sync nibble wasn't found. Raw");
    }

    return 0;
}

// ceil((335 + 11) / 8)
#define EXPECTED_NUM_BYTES 44

/**
Various Oregon Scientific protocols.

@todo Documentation needed.
*/
static int oregon_scientific_v3_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b = bitbuffer->bb[0];
    data_t *data;

    // Check stream for possible Oregon Scientific v3 protocol preamble
    if ((((b[0]&0xf) != 0x0f) || (b[1] != 0xff) || ((b[2]&0xc0) != 0xc0))
            && (((b[0]&0xf) != 0x00) || (b[1] != 0x00) || ((b[2]&0xc0) != 0x00))) {
        if (b[3] != 0) {
            decoder_log_bitrow(decoder, 1, __func__, b, bitbuffer->bits_per_row[0], "Unrecognized Msg in OS v3");
        }
        return DECODE_ABORT_EARLY;
    }

    unsigned char msg[EXPECTED_NUM_BYTES] = {0};
    int msg_pos = 0;
    int msg_len = 0;

    // e.g. WGR800X has {335} 00 00 00 b1 22 40 0e 00 06 00 00 00 19 7c   00 00 00 b1 22 40 0e 00 06 00 00 00 19 7c   00 00 00 b1 22 40 0e 00 06 00 00 00 19 7c
    // aligned (at 11) and reflected that's 3 packets:
    // {324} 00 0a 19 84 00 e0 00 c0 00 00 00 3d 70   00 00 0a 19 84 00 e0 00 c0 00 00 00 3d 70   00 00 0a 19 84 00 e0 00 c0 00 00 00 3d 70

    // full preamble is 00 00 00 5 (shorter for WGR800X)
    uint8_t const os_pattern[] = {0x00, 0x05};
    // CM180 preamble is 00 00 00 46, with 0x46 already data
    uint8_t const cm180_pattern[] = {0x00, 0x46};
    uint8_t const cm180i_pattern[] = {0x00, 0x4A};
    // workaround for a broken manchester demod
    // CM160 preamble might look like 7f ff ff aa, i.e. ff ff f5
    uint8_t const alt_pattern[] = {0xff, 0xf5};

    int os_pos    = bitbuffer_search(bitbuffer, 0, 0, os_pattern, 16) + 16;
    int cm180_pos = bitbuffer_search(bitbuffer, 0, 0, cm180_pattern, 16) + 8; // keep the 0x46
    int cm180i_pos = bitbuffer_search(bitbuffer, 0, 0, cm180i_pattern, 16) + 8; // keep the 0x46
    int alt_pos   = bitbuffer_search(bitbuffer, 0, 0, alt_pattern, 16) + 16;

    if (bitbuffer->bits_per_row[0] - os_pos >= 7 * 8) {
        msg_pos = os_pos;
        msg_len = bitbuffer->bits_per_row[0] - os_pos;
    }

    // 52 bits: secondary frame (instant watts only)
    // 108 bits: primary frame (instant watts + cumulative wattshour)
    else if (bitbuffer->bits_per_row[0] - cm180_pos >= 52) {
        msg_pos = cm180_pos;
        msg_len = bitbuffer->bits_per_row[0] - cm180_pos;
    }

    else if (bitbuffer->bits_per_row[0] - cm180i_pos >= 84) {
        msg_pos = cm180i_pos;
        msg_len = bitbuffer->bits_per_row[0] - cm180i_pos;
    }

    else if (bitbuffer->bits_per_row[0] - alt_pos >= 7 * 8) {
        msg_pos = alt_pos;
        msg_len = bitbuffer->bits_per_row[0] - alt_pos;
    }

    if (msg_len == 0 || msg_len > (int)sizeof(msg) * 8)
        return DECODE_ABORT_EARLY;

    bitbuffer_extract_bytes(bitbuffer, 0, msg_pos, msg, msg_len);
    reflect_nibbles(msg, (msg_len + 7) / 8);

    int sensor_id   = (msg[0] << 8) | msg[1];            // not for CM sensor types
    int channel     = (msg[2] >> 4) & 0x0f;              // not for CM sensor types
    int device_id   = (msg[2] & 0x0f) | (msg[3] & 0xf0); // not for CM sensor types
    int battery_low = (msg[3] >> 2) & 0x01;              // not for CM sensor types

    if (sensor_id == ID_THGR810 || sensor_id == ID_THGR810a) {
        if (validate_os_checksum(decoder, msg, 15) != 0)
            return DECODE_FAIL_MIC;
        // Sanity check BCD digits
        if (((msg[5] >> 4) & 0x0F) > 9 || (msg[4] & 0x0F) > 9 || ((msg[4] >> 4) & 0x0F) > 9 || (msg[6] & 0x0F) > 9 || ((msg[6] >> 4) & 0x0F) > 9) {
            decoder_log(decoder, 1, __func__, "THGR810 Message failed BCD sanity check.");
            return DECODE_FAIL_SANITY;
        }
        float temp_c = get_os_temperature(msg);
        int humidity = get_os_humidity(msg);
        // Sanity check values
        if (temp_c > 70 || temp_c < -50 || humidity < 0 || humidity > 98) {
            decoder_logf(decoder, 1, __func__, "THGR810 Message failed values sanity check: temperature_C %3.1fC humidity %d%%.", temp_c, humidity);
            return DECODE_FAIL_SANITY;
        }
        /* clang-format off */
        data = data_make(
                "model",                    "",                     DATA_STRING, "Oregon-THGR810",
                "id",                         "House Code", DATA_INT,        device_id,
                "channel",                "Channel",        DATA_INT,        channel,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                "humidity",             "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;                                    //msg[k] = ((msg[k] & 0x0F) << 4) + ((msg[k] & 0xF0) >> 4);
    }
    else if (sensor_id == ID_THN802) {
        if (validate_os_checksum(decoder, msg, 12) != 0)
            return DECODE_FAIL_MIC;
        float temp_c = get_os_temperature(msg);
        /* clang-format off */
        data = data_make(
                "model",                    "",                     DATA_STRING, "Oregon-THN802",
                "id",                         "House Code", DATA_INT,        device_id,
                "channel",                "Channel",        DATA_INT,        channel,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_UV800) {
        if (validate_os_checksum(decoder, msg, 13) != 0)
            return DECODE_FAIL_MIC;
        int uvidx = get_os_uv(msg);
        /* clang-format off */
        data = data_make(
                "model",                    "",                     DATA_STRING, "Oregon-UV800",
                "id",                         "House Code", DATA_INT,        device_id,
                "channel",                "Channel",        DATA_INT,        channel,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "uv",                         "UV Index",     DATA_FORMAT, "%u", DATA_INT, uvidx,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_PCR800) {
        if (validate_os_checksum(decoder, msg, 18) != 0)
            return DECODE_FAIL_MIC;
        // Sanity check BCD digits
        if ((msg[8] & 0x0F) > 9
                || ((msg[8] >> 4) & 0x0F) > 9
                || (msg[7] & 0x0F) > 9
                || ((msg[7] >> 4) & 0x0F) > 9
                || (msg[6] & 0x0F) > 9
                || ((msg[6] >> 4) & 0x0F) > 9
                || (msg[5] & 0x0F) > 9
                || ((msg[5] >> 4) & 0x0F) > 9
                || (msg[4] & 0x0F) > 9
                || ((msg[4] >> 4) & 0x0F) > 9) {
            decoder_log(decoder, 1, __func__, "PCR800 Message failed BCD sanity check.");
            return DECODE_FAIL_SANITY;
        }

        float rain_rate = get_os_rain_rate(msg);
        float total_rain = get_os_total_rain(msg);

        /* clang-format off */
        data = data_make(
                "model",            "",                     DATA_STRING, "Oregon-PCR800",
                "id",                 "House Code", DATA_INT,        device_id,
                "channel",        "Channel",        DATA_INT,        channel,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "rain_rate_in_h",    "Rain Rate",    DATA_FORMAT, "%5.1f in/h", DATA_DOUBLE, rain_rate,
                "rain_in", "Total Rain", DATA_FORMAT, "%7.3f in", DATA_DOUBLE, total_rain,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_PCR800a) {
        if (validate_os_checksum(decoder, msg, 18) != 0)
            return DECODE_FAIL_MIC;
        float rain_rate = get_os_rain_rate(msg);
        float total_rain = get_os_total_rain(msg);
        /* clang-format off */
        data = data_make(
                "model",            "",                     DATA_STRING, "Oregon-PCR800a",
                "id",                 "House Code", DATA_INT,        device_id,
                "channel",        "Channel",        DATA_INT,        channel,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "rain_rate_in_h",    "Rain Rate",    DATA_FORMAT, "%3.1f in/h", DATA_DOUBLE, rain_rate,
                "rain_in", "Total Rain", DATA_FORMAT, "%3.1f in", DATA_DOUBLE, total_rain,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (sensor_id == ID_WGR800 || sensor_id == ID_WGR800a) {
        if (validate_os_checksum(decoder, msg, 17) != 0)
            return DECODE_FAIL_MIC;
        // Sanity check BCD digits
        if ((msg[5] & 0x0F) > 9
                || ((msg[6] >> 4) & 0x0F) > 9
                || (msg[6] & 0x0F) > 9
                || ((msg[7] >> 4) & 0x0F) > 9
                || (msg[7] & 0x0F) > 9
                || ((msg[8] >> 4) & 0x0F) > 9) {
            decoder_log(decoder, 1, __func__, "WGR800 Message failed BCD sanity check.");
            return DECODE_FAIL_SANITY;
        }

        float gustWindspeed = (msg[5]&0x0f) /10.0F + ((msg[6]>>4)&0x0f) *1.0F + (msg[6]&0x0f) * 10.0F;
        float avgWindspeed = ((msg[7]>>4)&0x0f) / 10.0F + (msg[7]&0x0f) *1.0F + ((msg[8]>>4)&0x0f) * 10.0F;
        float quadrant = (0x0f&(msg[4]>>4))*22.5F;

        // Sanity check values
        if (gustWindspeed < 0 || gustWindspeed > 56 || avgWindspeed < 0 || avgWindspeed > 56 || quadrant < 0 || quadrant > 337.5) {
            decoder_logf(decoder, 1, __func__, "WGR800 Message failed values sanity check: wind_max_m_s %2.1f wind_avg_m_s %2.1f wind_dir_deg %3.1f.", gustWindspeed, avgWindspeed, quadrant);
            return DECODE_FAIL_SANITY;
        }

        /* clang-format off */
        data = data_make(
                "model",            "",                     DATA_STRING,    "Oregon-WGR800",
                "id",                 "House Code", DATA_INT,         device_id,
                "channel",        "Channel",        DATA_INT,         channel,
                "battery_ok",          "Battery",         DATA_INT,    !battery_low,
                "wind_max_m_s",             "Gust",             DATA_FORMAT,    "%2.1f m/s",DATA_DOUBLE, gustWindspeed,
                "wind_avg_m_s",        "Average",        DATA_FORMAT,    "%2.1f m/s",DATA_DOUBLE, avgWindspeed,
                "wind_dir_deg",    "Direction",    DATA_FORMAT,    "%3.1f degrees",DATA_DOUBLE, quadrant,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if ((msg[0] == 0x20) || (msg[0] == 0x21) || (msg[0] == 0x22) || (msg[0] == 0x23) || (msg[0] == 0x24)) { // Owl CM160 Readings
        msg[0] = msg[0] & 0x0F;

        if (validate_os_checksum(decoder, msg, 22) != 0)
            return DECODE_FAIL_MIC;

        int id = msg[1] & 0x0F;

        unsigned int current_amps  = swap_nibbles(msg[3]) | ((msg[4] >> 4) << 8);
        double current_watts = current_amps * 0.07 * 230; // Assuming device is running in 230V country

        double total_amps = ((uint64_t)swap_nibbles(msg[10]) << 36) | ((uint64_t)swap_nibbles(msg[9]) << 28) |
                    (swap_nibbles(msg[8]) << 20) | (swap_nibbles(msg[7]) << 12) |
                    (swap_nibbles(msg[6]) << 4) | (msg[5]&0xf);

        double total_kWh = total_amps * 230.0 / 3600.0 / 1000.0 * 1.12; // Assuming device is running in 230V country
        //result compares to the CM160 LCD display values when * 1.12 between readings

        /* clang-format off */
        data = data_make(
                "model",            "",                     DATA_STRING,    "Oregon-CM160",
                "id",               "House Code",           DATA_INT, id,
 //               "current_A",        "Current Amps",         DATA_FORMAT,   "%d A", DATA_INT, current_amps,
 //               "total_As",         "Total Amps",           DATA_FORMAT,   "%d As", DATA_INT, (int)total_amps,
                "power_W",          "Power",                DATA_FORMAT,   "%7.4f W", DATA_DOUBLE, current_watts,
                "energy_kWh",       "Energy",               DATA_FORMAT, "%7.4f kWh",DATA_DOUBLE, total_kWh,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }
    else if (msg[0] == 0x26) { // Owl CM180 readings
        msg[0]    = msg[0] & 0x0f;
        int valid = validate_os_checksum(decoder, msg, 23);
        for (int k = 0; k < EXPECTED_NUM_BYTES; k++) { // Reverse nibbles
            msg[k] = (msg[k] & 0xF0) >> 4 | (msg[k] & 0x0F) << 4;
        }
        // TODO: should we return if valid == 0?

        int sequence = msg[1] & 0x0F;
        int id       = msg[2] << 8 | (msg[1] & 0xF0);
        int batt_low = (msg[3] & 0x1); // 8th bit instead of 6th commonly used for other devices

        unsigned ipower = cm180_power(msg);
        uint64_t itotal = cm180_total(msg);
        float total_energy        = itotal / 3600.0 / 1000.0;
        if (valid == 0) {
            /* clang-format off */
            data = data_make(
                    "model",            "",                 DATA_STRING, "Oregon-CM180",
                    "id",               "House Code",       DATA_INT,    id,
                    "battery_ok",       "Battery",          DATA_INT,    !batt_low,
                    "power_W",          "Power",            DATA_FORMAT, "%d W",DATA_INT, ipower,
                    "energy_kWh",       "Energy",           DATA_COND,   itotal != 0, DATA_FORMAT, "%2.2f kWh",DATA_DOUBLE, total_energy,
                    "sequence",         "sequence number",  DATA_INT,    sequence,
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            return 1;
        }
    }
    else if (msg[0] == 0x25) { // Owl CM180i readings
        int valid = 0;
        msg[0]    = msg[0] & 0x0f;
        // to be done
        // int valid = validate_os_checksum(decoder, msg, 23);
        for (int k = 0; k < EXPECTED_NUM_BYTES; k++) { // Reverse nibbles
            msg[k] = (msg[k] & 0xF0) >> 4 | (msg[k] & 0x0F) << 4;
        }
        // TODO: should we return if valid == 0?

        int sequence = msg[1] & 0x0F;
        int id       = msg[2] << 8 | (msg[1] & 0xF0);
        int batt_low = (msg[3] & 0x40)?1:0; // 8th bit instead of 6th commonly used for other devices

        unsigned ipower1 = cm180i_power(msg,0);
        unsigned ipower2 = cm180i_power(msg,1);
        unsigned ipower3 = cm180i_power(msg,2);
        uint64_t itotal= 0;
        if (msg_len >= 140) itotal= cm180i_total(msg);

        // Convert `itotal` which is in Ws (or J) to kWh unit.
        float total_energy        = itotal / 3600.0 / 1000.0;

        if (valid == 0) {
            /* clang-format off */
            data = data_make(
                    "model",            "",                 DATA_STRING, "Oregon-CM180i",
                    "id",               "House Code",       DATA_INT,    id,
                    "battery_ok",       "Battery",          DATA_INT,    !batt_low,
                    "power1_W",         "Power1",           DATA_FORMAT, "%d W",DATA_INT, ipower1,
                    "power2_W",         "Power2",           DATA_FORMAT, "%d W",DATA_INT, ipower2,
                    "power3_W",         "Power3",           DATA_FORMAT, "%d W",DATA_INT, ipower3,
                    "energy_kWh",       "Energy",           DATA_COND,   itotal != 0, DATA_FORMAT, "%2.2f kWh",DATA_DOUBLE, total_energy,
                    "sequence",         "sequence number",  DATA_INT,    sequence,
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            return 1;
        }
    }
    else if ((msg[0] != 0) && (msg[1] != 0)) { // sync nibble was found and some data is present...
        decoder_log(decoder, 1, __func__, "Message received from unrecognized Oregon Scientific v3 sensor.");
        decoder_log_bitrow(decoder, 1, __func__, msg, msg_len, "Message");
        decoder_log_bitrow(decoder, 1, __func__, b, bitbuffer->bits_per_row[0], "Raw");
    }
    else if (b[3] != 0) {
        decoder_log(decoder, 1, __func__, "Possible Oregon Scientific v3 message, but sync nibble wasn't found");
        decoder_log_bitrow(decoder, 1, __func__, b, bitbuffer->bits_per_row[0], "Raw Data");
    }
    return DECODE_FAIL_SANITY;
}

/**
Various Oregon Scientific protocols.
@sa oregon_scientific_v2_1_decode() oregon_scientific_v3_decode()
*/
static int oregon_scientific_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = oregon_scientific_v2_1_decode(decoder, bitbuffer);
    if (ret <= 0)
        ret = oregon_scientific_v3_decode(decoder, bitbuffer);
    return ret;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "rain_rate", // TODO: remove this
        "rain_rate_mm_h",
        "rain_rate_in_h",
        "rain_total", // TODO: remove this
        "rain_mm",
        "rain_in",
        "gust",      // TODO: remove this
        "average",   // TODO: remove this
        "direction", // TODO: remove this
        "wind_max_m_s",
        "wind_avg_m_s",
        "wind_dir_deg",
        "pressure_hPa",
        "uv",
        "power_W",
        "energy_kWh",
        "radio_clock",
        "sequence",
        NULL,
};

r_device const oregon_scientific = {
        .name        = "Oregon Scientific Weather Sensor",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 440, // Nominal 1024Hz (488us), but pulses are shorter than pauses
        .long_width  = 0,   // not used
        .reset_limit = 2400,
        .decode_fn   = &oregon_scientific_decode,
        .fields      = output_fields,
};
