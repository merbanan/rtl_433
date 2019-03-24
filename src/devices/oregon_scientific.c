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

/// Documentation for Oregon Scientific protocols can be found here:
/// http://wmrx00.sourceforge.net/Arduino/OregonScientific-RF-Protocols.pdf
// Sensors ID
#define ID_THGR122N 0x1d20
#define ID_THGR968  0x1d30
#define ID_BHTR968  0x5d60
#define ID_RGR968   0x2d10
#define ID_THR228N  0xec40
#define ID_THN132N  0xec40 // same as THR228N but different packet size
#define ID_RTGN318  0x0cc3 // warning: id is from 0x0cc3 and 0xfcc3
#define ID_RTGN129  0x0cc3 // same as RTGN318 but different packet size
#define ID_THGR810  0xf824
#define ID_THN802   0xc844
#define ID_PCR800   0x2914
#define ID_PCR800a  0x2d14 // Different PCR800 ID - AU version I think
#define ID_THGR81   0xf824
#define ID_WGR800   0x1984
#define ID_WGR968   0x3d00
#define ID_UV800    0xd874
#define ID_THN129   0xcc43 // THN129 Temp only
#define ID_RTHN129  0x0cd3 // RTHN129 Temp, clock sensors
#define ID_BTHGN129 0x5d53 // Baro, Temp, Hygro sensor
#define ID_UVR128   0xec70

float get_os_temperature(unsigned char *message, unsigned int sensor_id)
{
    // sensor ID included    to support sensors with temp in different position
    float temp_c = 0;
    temp_c = (((message[5]>>4)*100)+((message[4]&0x0f)*10) + ((message[4]>>4)&0x0f)) / 10.0F;
    if (message[5] & 0x0f)
        temp_c = -temp_c;
    return temp_c;
}

float get_os_rain_rate(unsigned char *message, unsigned int sensor_id)
{
    float rain_rate = 0;        // Nibbles 11..8 rain rate, LSD = 0.01 inches per hour
    rain_rate = (((message[5]&0x0f) * 1000) +((message[5]>>4)*100)+((message[4]&0x0f)*10) + ((message[4]>>4)&0x0f)) / 100.0F;
    return rain_rate;
}

float get_os_total_rain(unsigned char *message, unsigned int sensor_id)
{
    float total_rain = 0.0F; // Nibbles 17..12 Total rain, LSD = 0.001, 543210 = 012.345 inches
    total_rain = (message[8]&0x0f) * 100.0F +((message[8]>>4)&0x0f)*10.0F     +(message[7]&0x0f)
     + ((message[7]>>4)&0x0f) / 10.0F + (message[6]&0x0f) / 100.0F + ((message[6]>>4)&0x0f)/1000.0F;
    return total_rain;
}

unsigned int get_os_humidity(unsigned char *message, unsigned int sensor_id)
{
    // sensor ID included to support sensors with humidity in different position
    int humidity = 0;
    humidity = ((message[6]&0x0f)*10)+(message[6]>>4);
    return humidity;
}

float get_os_pressure(r_device *decoder, unsigned char *message, unsigned int sensor_id)
{
    // sensor ID included to support sensors with pressure in different position/format
    if (decoder->verbose) {
        fprintf(stdout, " raw pressure data : %02x %02x\n", (int)message[8], (int)message[7]);
    }
    // Pressure is given in inHg, but we really can't use that.
    // Let's convert to hPa. 1 inHg equals 33.8639 HPa
    float pressure = ((message[8]<<4)+message[7]) / 100.0 * 33.8639;
    return pressure;
}


unsigned int get_os_uv(unsigned char *message, unsigned int sensor_id)
{
    // sensor ID included to support sensors with uv in different position
    int uvidx = 0;
    uvidx = ((message[4]&0x0f)*10)+(message[4]>>4);
    return uvidx;
}

unsigned int get_os_channel(unsigned char *message, unsigned int sensor_id)
{
    // sensor ID included to support sensors with channel in different position
    int channel = 0;
    channel = ((message[2] >> 4)&0x0f);
    if ((channel == 4) && (sensor_id & 0x0fff) != ID_RTGN318 && sensor_id != ID_THGR810 && (sensor_id & 0x0fff) != ID_RTHN129)
        channel = 3; // sensor 3 channel number is 0x04
    return channel;
}

unsigned int get_os_battery(unsigned char *message, unsigned int sensor_id)
{
    // sensor ID included to support sensors with battery in different position
    int battery_low = 0;
    battery_low = (message[3] >> 2 & 0x01);
    return battery_low;
}

unsigned int get_os_rollingcode(unsigned char *message, unsigned int sensor_id)
{
    // sensor ID included to support sensors with rollingcode in different position
    int rc = 0;
    rc = (message[2]&0x0F) + (message[3]&0xF0);
    return rc;
}

unsigned short int power(const unsigned char* d)
{
    unsigned short int val = 0;
    val = (d[4] << 8) + d[3];
    val = val & 0xFFF0;
    val = val * 1.00188;
    return val;
}

unsigned long long total(const unsigned char* d)
{
    unsigned long long val = 0;
    if ((d[1]&0x0F) == 0) {
        // Sensor returns total only if nibble#4 == 0
        val = (unsigned long long)d[10]<< 40;
        val += (unsigned long long)d[9] << 32;
        val += (unsigned long)d[8]<<24;
        val += (unsigned long)d[7] << 16;
        val += d[6] << 8;
        val += d[5];
    }
    return val;
}

static int validate_os_checksum(r_device *decoder, unsigned char *msg, int checksum_nibble_idx)
{
    // Oregon Scientific v2.1 and v3 checksum is a    1 byte    'sum of nibbles' checksum.
    // with the 2 nibbles of the checksum byte    swapped.
    int i;
    unsigned int checksum, sum_of_nibbles=0;
    for (i=0; i<(checksum_nibble_idx-1);i+=2) {
        unsigned char val=msg[i>>1];
        sum_of_nibbles += ((val>>4) + (val &0x0f));
    }
    if (checksum_nibble_idx & 1) {
        sum_of_nibbles += (msg[checksum_nibble_idx>>1]>>4);
        checksum = (msg[checksum_nibble_idx>>1] & 0x0f) | (msg[(checksum_nibble_idx+1)>>1]&0xf0);
    }
    else {
        checksum = (msg[checksum_nibble_idx>>1]>>4) | ((msg[checksum_nibble_idx>>1]&0x0f)<<4);
    }
    sum_of_nibbles &= 0xff;

    if (sum_of_nibbles == checksum) {
        return 0;
    }
    else {
        if (decoder->verbose) {
            fprintf(stderr, "Checksum error in Oregon Scientific message.    Expected: %02x    Calculated: %02x\n", checksum, sum_of_nibbles);
            bitrow_printf(msg, ((checksum_nibble_idx + 4) >> 1) * 8, "Message: ");
        }
        return 1;
    }
}

static int validate_os_v2_message(r_device *decoder, unsigned char *msg, int bits_expected, int valid_v2_bits_received,
        int nibbles_in_checksum)
{
    // Oregon scientific v2.1 protocol sends each bit using the complement of the bit, then the bit    for better error checking.    Compare number of valid bits processed vs number expected
    if (bits_expected == valid_v2_bits_received) {
        return (validate_os_checksum(decoder, msg, nibbles_in_checksum));
    }
    else {
        if (decoder->verbose) {
            fprintf(stderr, "Bit validation error on Oregon Scientific message.    Expected %d bits, received error after bit %d \n",                bits_expected, valid_v2_bits_received);
            bitrow_printf(msg, bits_expected, "Message: ");
        }
    }
    return 1;
}

static int oregon_scientific_v2_1_parser(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitrow_t *bb = bitbuffer->bb;
    // Check 2nd and 3rd bytes of stream for possible Oregon Scientific v2.1 sensor data (skip first byte to get past sync/startup bit errors)
    if (((bb[0][1] == 0x55) && (bb[0][2] == 0x55)) ||
            ((bb[0][1] == 0xAA) && (bb[0][2] == 0xAA))) {
        int i,j;
        unsigned char msg[BITBUF_COLS] = {0};

        // Possible    v2.1 Protocol message
        int num_valid_v2_bits = 0;

        unsigned int sync_test_val = ((unsigned)bb[0][3]<<24) | (bb[0][4]<<16) | (bb[0][5]<<8) | (bb[0][6]);
        int dest_bit = 0;
        int pattern_index;
        // Could be extra/dropped bits in stream.    Look for sync byte at expected position +/- some bits in either direction
        for (pattern_index=0; pattern_index<8; pattern_index++) {
            unsigned int mask         = (unsigned int) (0xffff0000>>pattern_index);
            unsigned int pattern    = (unsigned int)(0x55990000>>pattern_index);
            unsigned int pattern2 = (unsigned int)(0xaa990000>>pattern_index);

            if (decoder->verbose) {
                fprintf(stdout, "OS v2.1 sync byte search - test_val=%08x pattern=%08x    mask=%08x\n", sync_test_val, pattern, mask);
            }

            if (((sync_test_val & mask) == pattern) ||
                    ((sync_test_val & mask) == pattern2)) {

                //    Found sync byte - start working on decoding the stream data.
                // pattern_index indicates    where sync nibble starts, so now we can find the start of the payload
                int start_byte = 5 + (pattern_index>>3);
                int start_bit = pattern_index & 0x07;
                if (decoder->verbose) {
                    fprintf(stdout, "OS v2.1 Sync test val %08x found, starting decode at byte index %d bit %d\n", sync_test_val, start_byte, start_bit);
                }
                int bits_processed = 0;
                unsigned char last_bit_val = 0;
                j=start_bit;
                for (i=start_byte;i<BITBUF_COLS;i++) {
                    while (j<8) {
                        if (bits_processed & 0x01) {
                            unsigned char bit_val = ((bb[0][i] & (0x80 >> j)) >> (7-j));

                            // check if last bit received was the complement of the current bit
                            if ((num_valid_v2_bits == 0) && (last_bit_val == bit_val))
                                num_valid_v2_bits = bits_processed; // record position of first bit in stream that doesn't verify correctly
                            last_bit_val = bit_val;

                            // copy every other bit from source stream to dest packet
                            msg[dest_bit>>3] |= (((bb[0][i] & (0x80 >> j)) >> (7-j)) << (7-(dest_bit & 0x07)));

                            //fprintf(stdout,"i=%d j=%d dest_bit=%02x bb=%02x msg=%02x\n",i, j, dest_bit, bb[0][i], msg[dest_bit>>3]);
                            if ((dest_bit & 0x07) == 0x07) {
                                // after assembling each dest byte, flip bits in each nibble to convert from lsb to msb bit ordering
                                int k = (dest_bit>>3);
                                unsigned char indata = msg[k];
                                // flip the 4 bits in the upper and lower nibbles
                                msg[k] = ((indata & 0x11) << 3) | ((indata & 0x22) << 1) |
                                        ((indata & 0x44) >> 1) | ((indata & 0x88) >> 3);
                            }
                            dest_bit++;
                        }
                        else {
                            last_bit_val = ((bb[0][i] & (0x80 >> j)) >> (7-j)); // used for v2.1 bit error detection
                        }
                        bits_processed++;
                        j++;
                    }
                    j=0;
                }
                break;
            } //if (sync_test_val...
        } // for (pattern...

        data_t *data;

        int sensor_id = (msg[0] << 8) | msg[1];
        if ((sensor_id == ID_THGR122N) || (sensor_id == ID_THGR968)) {
            if (validate_os_v2_message(decoder, msg, 153, num_valid_v2_bits, 15) == 0) {
                data = data_make(
                        "brand",                 "",                        DATA_STRING, "OS",
                        "model",                 "",                        DATA_STRING, (sensor_id == ID_THGR122N) ? _X("Oregon-THGR122N","THGR122N"): _X("Oregon-THGR968","THGR968"),
                        "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg, sensor_id),
                        "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id),
                        "battery",             "Battery",         DATA_STRING, get_os_battery(msg, sensor_id) ? "LOW" : "OK",
                        "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, get_os_temperature(msg, sensor_id),
                        "humidity",            "Humidity",        DATA_FORMAT, "%u %%",     DATA_INT,        get_os_humidity(msg, sensor_id),
                        NULL);
                decoder_output_data(decoder, data);
            }
            return 1;
        }
        else if (sensor_id == ID_WGR968) {
            if (validate_os_v2_message(decoder, msg, 189, num_valid_v2_bits, 17) == 0) {
                float quadrant = (((msg[4] &0x0f)*10) + ((msg[4]>>4)&0x0f) + (((msg[5]>>4)&0x0f) * 100));
                float avgWindspeed = ((msg[7]>>4)&0x0f) / 10.0F + (msg[7]&0x0f) *1.0F + ((msg[8]>>4)&0x0f) / 10.0F;
                float gustWindspeed = (msg[5]&0x0f) /10.0F + ((msg[6]>>4)&0x0f) *1.0F + (msg[6]&0x0f) / 10.0F;
                data = data_make(
                        "brand",            "",                     DATA_STRING, "OS",
                        "model",            "",                     DATA_STRING, _X("Oregon-WGR968","WGR968"),
                        "id",                 "House Code", DATA_INT,        get_os_rollingcode(msg, sensor_id),
                        "channel",        "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                        "battery",        "Battery",        DATA_STRING, get_os_battery(msg, sensor_id) ? "LOW" : "OK",
                        "gust",             "Gust",             DATA_FORMAT, "%2.1f m/s",DATA_DOUBLE, gustWindspeed,
                        "average",        "Average",        DATA_FORMAT, "%2.1f m/s",DATA_DOUBLE, avgWindspeed,
                        "direction",    "Direction",    DATA_FORMAT, "%3.1f degrees",DATA_DOUBLE, quadrant,
                        NULL);
                decoder_output_data(decoder, data);
            }
            return 1;
        }
        else if (sensor_id == ID_BHTR968) {
            if (validate_os_v2_message(decoder, msg, 185, num_valid_v2_bits, 19) == 0) {
                unsigned int comfort = msg[7] >>4;
                char *comfort_str="Normal";
                if            (comfort == 4)     comfort_str = "Comfortable";
                else if (comfort == 8)     comfort_str = "Dry";
                else if (comfort == 0xc) comfort_str = "Humid";
                unsigned int forecast = msg[9]>>4;
                char *forecast_str="Cloudy";
                if            (forecast == 3)     forecast_str = "Rainy";
                else if (forecast == 6)     forecast_str = "Partly Cloudy";
                else if (forecast == 0xc) forecast_str = "Sunny";
                float temp_c = get_os_temperature(msg, sensor_id);
                float pressure = ((msg[7] & 0x0f) | (msg[8] & 0xf0)) + 856;
                // fprintf(stdout,"Weather Sensor BHTR968    Indoor        Temp: %3.1fC    %3.1fF     Humidity: %d%%", temp_c, ((temp_c*9)/5)+32, get_os_humidity(msg, sensor_id));
                // fprintf(stdout, " (%s) Pressure: %dmbar (%s)\n", comfort_str, ((msg[7] & 0x0f) | (msg[8] & 0xf0))+856, forecast_str);
                data = data_make(
                        "brand",            "",                             DATA_STRING, "OS",
                        "model",            "",                             DATA_STRING, _X("Oregon-BHTR968","BHTR968"),
                        "id",                 "House Code",         DATA_INT,        get_os_rollingcode(msg, sensor_id),
                        "channel",        "Channel",                DATA_INT,        get_os_channel(msg, sensor_id),
                        "battery",        "Battery",                DATA_STRING, get_os_battery(msg, sensor_id) ? "LOW" : "OK",
                        "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                        "humidity",     "Humidity",             DATA_FORMAT, "%u %%",     DATA_INT,        get_os_humidity(msg, sensor_id),
                        "pressure_hPa",    "Pressure",        DATA_FORMAT, "%.0f hPa",     DATA_DOUBLE, pressure,
                        NULL);
                decoder_output_data(decoder, data);
            }
            return 1;
        }
        else if (sensor_id == ID_RGR968) {
            if (validate_os_v2_message(decoder, msg, 161, num_valid_v2_bits, 16) == 0) {
                float rain_rate = (((msg[4] &0x0f)*100)+((msg[4]>>4)*10) + ((msg[5]>>4)&0x0f)) /10.0F;
                float total_rain = (((msg[7]&0xf)*10000)+((msg[7]>>4)*1000) + ((msg[6]&0xf)*100)+((msg[6]>>4)*10) + (msg[5]&0xf))/10.0F;

                data = data_make(
                        "brand",            "",                     DATA_STRING, "OS",
                        "model",            "",                     DATA_STRING, _X("Oregon-RGR968","RGR968"),
                        "id",                 "House Code", DATA_INT,        get_os_rollingcode(msg, sensor_id),
                        "channel",        "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                        "battery",        "Battery",        DATA_STRING, get_os_battery(msg, sensor_id) ? "LOW" : "OK",
                        "rain_rate",    "Rain Rate",    DATA_FORMAT, "%.02f mm/hr", DATA_DOUBLE, rain_rate,
                        "total_rain", "Total Rain", DATA_FORMAT, "%.02f mm", DATA_DOUBLE, total_rain,
                        NULL);
                decoder_output_data(decoder, data);
            }
            return 1;
        }
        else if (sensor_id == ID_THR228N && num_valid_v2_bits==153) {
            if (validate_os_v2_message(decoder, msg, 153, num_valid_v2_bits, 12) == 0) {

                float temp_c = get_os_temperature(msg, sensor_id);
                data = data_make(
                        "brand",                 "",                        DATA_STRING, "OS",
                        "model",                 "",                        DATA_STRING, _X("Oregon-THR228N","THR228N"),
                        "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg, sensor_id),
                        "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id),
                        "battery",             "Battery",         DATA_STRING, get_os_battery(msg, sensor_id) ? "LOW" : "OK",
                        "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                        NULL);
                decoder_output_data(decoder, data);
            }
            return 1;
        }
        else if (sensor_id == ID_THN132N && num_valid_v2_bits==129) {
            if (validate_os_v2_message(decoder, msg, 129, num_valid_v2_bits, 12) == 0) {

                float temp_c = get_os_temperature(msg, sensor_id);
                data = data_make(
                        "brand",                 "",                        DATA_STRING, "OS",
                        "model",                 "",                        DATA_STRING, _X("Oregon-THN132N","THN132N"),
                        "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg, sensor_id),
                        "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id),
                        "battery",             "Battery",         DATA_STRING, get_os_battery(msg, sensor_id) ? "LOW" : "OK",
                        "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                        NULL);
                decoder_output_data(decoder, data);
            }
            return 1;
        }
        else if ((sensor_id & 0x0fff) == ID_RTGN129 && num_valid_v2_bits==161) {
            if (validate_os_v2_message(decoder, msg, 161, num_valid_v2_bits, 15) == 0) {
                float temp_c = get_os_temperature(msg, sensor_id);
                data = data_make(
                        "brand",                 "",                        DATA_STRING, "OS",
                        "model",                 "",                        DATA_STRING, _X("Oregon-RTGN129","RTGN129"),
                        "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg, sensor_id),
                        "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id), // 1 to 5
                        "battery",             "Battery",         DATA_STRING, get_os_battery(msg, sensor_id) ? "LOW" : "OK",
                        "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                        "humidity",            "Humidity",        DATA_FORMAT, "%u %%",     DATA_INT,        get_os_humidity(msg, sensor_id),
                        NULL);
                decoder_output_data(decoder, data);
            }
            return 1;
        }
        else if ((sensor_id & 0x0fff) == ID_RTGN318) {
            if (num_valid_v2_bits==153 && (validate_os_v2_message(decoder, msg, 153, num_valid_v2_bits, 15) == 0)) {
                float temp_c = get_os_temperature(msg, sensor_id);
                data = data_make(
                        "brand",                 "",                        DATA_STRING, "OS",
                        "model",                 "",                        DATA_STRING, _X("Oregon-RTGN318","RTGN318"),
                        "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg, sensor_id),
                        "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id), // 1 to 5
                        "battery",             "Battery",         DATA_STRING, get_os_battery(msg, sensor_id) ? "LOW" : "OK",
                        "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                        "humidity",            "Humidity",        DATA_FORMAT, "%u %%",     DATA_INT,        get_os_humidity(msg, sensor_id),
                        NULL);
                decoder_output_data(decoder, data);
            }
            else if (num_valid_v2_bits==201 && (validate_os_v2_message(decoder, msg, 201, num_valid_v2_bits, 21) == 0)) {

                // RF Clock message ??
            }
            return 1;
        }
        else if (sensor_id == ID_THN129 || (sensor_id & 0x0FFF) == ID_RTHN129) {
            if ((validate_os_v2_message(decoder, msg, 137, num_valid_v2_bits, 12) == 0)) {
                float temp_c = get_os_temperature(msg, sensor_id);
                data = data_make(
                        "brand",                 "",                        DATA_STRING, "OS",
                        "model",                 "",                        DATA_STRING, (sensor_id == ID_THN129) ? _X("Oregon-THN129","THN129") : "Oregon-RTHN129",
                        "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg, sensor_id),
                        "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id), // 1 to 5
                        "battery",             "Battery",         DATA_STRING, get_os_battery(msg, sensor_id) ? "LOW" : "OK",
                        "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                        NULL);
                decoder_output_data(decoder, data);

            }
            else if (num_valid_v2_bits == 209 && (validate_os_v2_message(decoder, msg, 209, num_valid_v2_bits, 18) == 0)) {
                // RF Clock message
            }

            return 1;
        }
        else if (sensor_id    == ID_BTHGN129) {
            //if ((validate_os_v2_message(decoder, msg, 137, num_valid_v2_bits, 12) == 0)) {
                float temp_c = get_os_temperature(msg, sensor_id);
                float pressure = get_os_pressure(decoder, msg, sensor_id);
                data = data_make(
                        "brand",                 "",                        DATA_STRING, "OS",
                        "model",                 "",                        DATA_STRING, _X("Oregon-BTHGN129","BTHGN129"),
                        "id",                        "House Code",    DATA_INT,        get_os_rollingcode(msg, sensor_id),
                        "channel",             "Channel",         DATA_INT,        get_os_channel(msg, sensor_id), // 1 to 5
                        "battery",             "Battery",         DATA_STRING, get_os_battery(msg, sensor_id) ? "LOW" : "OK",
                        "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                        "humidity",             "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, get_os_humidity(msg, sensor_id),
                        "pressure_hPa",    "Pressure",        DATA_FORMAT, "%.02f hPa", DATA_DOUBLE, pressure,
                        NULL);
                decoder_output_data(decoder, data);
            //}

            return 1;
        }
        else if (sensor_id == ID_UVR128 && num_valid_v2_bits==297) {
                if ((validate_os_v2_message(decoder, msg, 297, num_valid_v2_bits, 12) == 0)) {
                int uvidx = get_os_uv(msg, sensor_id);
                data = data_make(
                    "model",                    "",                     DATA_STRING, _X("Oregon-UVR128","Oregon Scientific UVR128"),
                    "id",                         "House Code", DATA_INT,        get_os_rollingcode(msg, sensor_id),
                    "uv",                         "UV Index",     DATA_FORMAT, "%u", DATA_INT, uvidx,
                    "battery",                "Battery",        DATA_STRING, get_os_battery(msg, sensor_id)?"LOW":"OK",
                    //"channel",                "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                    NULL);
                decoder_output_data(decoder, data);
                }
         return 1;
        }
        else if (num_valid_v2_bits > 16) {
            if (decoder->verbose) {
                fprintf(stdout, "%d bit message received from unrecognized Oregon Scientific v2.1 sensor with device ID %x.\n", num_valid_v2_bits, sensor_id);
                bitrow_printf(msg, 20 * 8, "Message: ");
            }
        }
        else {
            if (decoder->verbose) {
                fprintf(stdout, "\nPossible Oregon Scientific v2.1 message, but sync nibble wasn't found\n");
                bitrow_printf(bb[0], bitbuffer->bits_per_row[0], "Raw Data: ");
            }
        }
    }
    else {
        if (bb[0][3] != 0) {
            if (decoder->verbose) {
                fprintf(stdout, "\nBadly formatted OS v2.1 message encountered.\n");
                bitrow_printf(bb[0], bitbuffer->bits_per_row[0], "Raw Data: ");
            }
        }
}
return 0;
}

static int oregon_scientific_v3_parser(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitrow_t *bb = bitbuffer->bb;
    data_t *data;

    // Check stream for possible Oregon Scientific v3 protocol data (skip part of first and last bytes to get past sync/startup bit errors)
    if ((((bb[0][0]&0xf) == 0x0f) && (bb[0][1] == 0xff) && ((bb[0][2]&0xc0) == 0xc0)) ||
            (((bb[0][0]&0xf) == 0x00) && (bb[0][1] == 0x00) && ((bb[0][2]&0xc0) == 0x00))) {
        int i,j;
        unsigned char msg[BITBUF_COLS] = {0};
        unsigned int sync_test_val = ((unsigned)bb[0][2]<<24) | (bb[0][3]<<16) | (bb[0][4]<<8);
        int dest_bit = 0;
        int pattern_index;
        // Could be extra/dropped bits in stream.    Look for sync byte at expected position +/- some bits in either direction
        for (pattern_index=0; pattern_index<16; pattern_index++) {
            unsigned int         mask = (unsigned int)(0xfff00000>>pattern_index);
            unsigned int    pattern = (unsigned int)(0xffa00000>>pattern_index);
            unsigned int pattern2 = (unsigned int)(0xff500000>>pattern_index);
            unsigned int pattern3 = (unsigned int)(0x00500000>>pattern_index);
            unsigned int pattern4 = (unsigned int)(0x04600000>>pattern_index);
            //fprintf(stdout, "OS v3 Sync nibble search - test_val=%08x pattern=%08x    mask=%08x\n", sync_test_val, pattern, mask);
            if (((sync_test_val & mask) == pattern)    || ((sync_test_val & mask) == pattern2) ||
                    ((sync_test_val & mask) == pattern3) || ((sync_test_val & mask) == pattern4)) {
                // Found sync byte - start working on decoding the stream data.
                // pattern_index indicates    where sync nibble starts, so now we can find the start of the payload
                int start_byte = 3 + (pattern_index>>3);
                int start_bit = (pattern_index+4) & 0x07;
                //fprintf(stdout, "Oregon Scientific v3 Sync test val %08x ok, starting decode at byte index %d bit %d\n", sync_test_val, start_byte, start_bit);
                j = start_bit;
                for (i=start_byte;i<BITBUF_COLS;i++) {
                    while (j<8) {
                        unsigned char bit_val = ((bb[0][i] & (0x80 >> j)) >> (7-j));

                        // copy every    bit from source stream to dest packet
                        msg[dest_bit>>3] |= (((bb[0][i] & (0x80 >> j)) >> (7-j)) << (7-(dest_bit & 0x07)));

                        //fprintf(stdout,"i=%d j=%d dest_bit=%02x bb=%02x msg=%02x\n",i, j, dest_bit, bb[0][i], msg[dest_bit>>3]);
                        if ((dest_bit & 0x07) == 0x07) {
                            // after assembling each dest byte, flip bits in each nibble to convert from lsb to msb bit ordering
                            int k = (dest_bit>>3);
                            unsigned char indata = msg[k];
                            // flip the 4 bits in the upper and lower nibbles
                            msg[k] = ((indata & 0x11) << 3) | ((indata & 0x22) << 1) |
                                ((indata & 0x44) >> 1) | ((indata & 0x88) >> 3);
                        }
                        dest_bit++;
                        j++;
                    }
                    j=0;
                }
                break;
            }
        }
        int sensor_id = (msg[0] << 8) | msg[1];
        if (sensor_id == ID_THGR810) {
            if (validate_os_checksum(decoder, msg, 15) == 0) {
                float temp_c = get_os_temperature(msg, sensor_id);
                int humidity = get_os_humidity(msg, sensor_id);
                data = data_make(
                    "brand",                    "",                     DATA_STRING, "OS",
                    "model",                    "",                     DATA_STRING, _X("Oregon-THGR810","THGR810"),
                    "id",                         "House Code", DATA_INT,        get_os_rollingcode(msg, sensor_id),
                    "channel",                "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                    "battery",                "Battery",        DATA_STRING, get_os_battery(msg, sensor_id)?"LOW":"OK",
                    "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                    "humidity",             "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
                    NULL);
                decoder_output_data(decoder, data);
            }
            return 1;                                    //msg[k] = ((msg[k] & 0x0F) << 4) + ((msg[k] & 0xF0) >> 4);
        }
        else if (sensor_id == ID_THN802) {
                if (validate_os_checksum(decoder, msg, 12) == 0) {
                    float temp_c = get_os_temperature(msg, sensor_id);
                    data = data_make(
                        "brand",                    "",                     DATA_STRING, "OS",
                        "model",                    "",                     DATA_STRING, _X("Oregon-THN802","THN802"),
                        "id",                         "House Code", DATA_INT,        get_os_rollingcode(msg, sensor_id),
                        "channel",                "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                        "battery",                "Battery",        DATA_STRING, get_os_battery(msg, sensor_id)?"LOW":"OK",
                        "temperature_C",    "Celsius",        DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                        NULL);
                    decoder_output_data(decoder, data);
                }
                return 1;
        }
        else if (sensor_id == ID_UV800) {
            if (validate_os_checksum(decoder, msg, 13) == 0) {     // ok
                int uvidx = get_os_uv(msg, sensor_id);
                data = data_make(
                    "brand",                    "",                     DATA_STRING, "OS",
                    "model",                    "",                     DATA_STRING, _X("Oregon-UV800","UV800"),
                    "id",                         "House Code", DATA_INT,        get_os_rollingcode(msg, sensor_id),
                    "channel",                "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                    "battery",                "Battery",        DATA_STRING, get_os_battery(msg, sensor_id)?"LOW":"OK",
                    "uv",                         "UV Index",     DATA_FORMAT, "%u", DATA_INT, uvidx,
                    NULL);
                decoder_output_data(decoder, data);
            }
        }
        else if (sensor_id == ID_PCR800) {
            if (validate_os_checksum(decoder, msg, 18) == 0) {
                float rain_rate=get_os_rain_rate(msg, sensor_id);
                float total_rain=get_os_total_rain(msg, sensor_id);
                data = data_make(
                    "brand",            "",                     DATA_STRING, "OS",
                    "model",            "",                     DATA_STRING, _X("Oregon-PCR800","PCR800"),
                    "id",                 "House Code", DATA_INT,        get_os_rollingcode(msg, sensor_id),
                    "channel",        "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                    "battery",        "Battery",        DATA_STRING, get_os_battery(msg, sensor_id)?"LOW":"OK",
                    "rain_rate",    "Rain Rate",    DATA_FORMAT, "%3.1f in/hr", DATA_DOUBLE, rain_rate,
                    "rain_total", "Total Rain", DATA_FORMAT, "%3.1f in", DATA_DOUBLE, total_rain,
                    NULL);
                decoder_output_data(decoder, data);
            }
            return 1;
        }
        else if (sensor_id == ID_PCR800a) {
            if (validate_os_checksum(decoder, msg, 18) == 0) {
                float rain_rate=get_os_rain_rate(msg, sensor_id);
                float total_rain=get_os_total_rain(msg, sensor_id);
                data = data_make(
                        "brand",            "",                     DATA_STRING, "OS",
                        "model",            "",                     DATA_STRING, _X("Oregon-PCR800a","PCR800a"),
                        "id",                 "House Code", DATA_INT,        get_os_rollingcode(msg, sensor_id),
                        "channel",        "Channel",        DATA_INT,        get_os_channel(msg, sensor_id),
                        "battery",        "Battery",        DATA_STRING, get_os_battery(msg, sensor_id)?"LOW":"OK",
                        "rain_rate",    "Rain Rate",    DATA_FORMAT, "%3.1f in/hr", DATA_DOUBLE, rain_rate,
                        "rain_total", "Total Rain", DATA_FORMAT, "%3.1f in", DATA_DOUBLE, total_rain,
                        NULL);
                decoder_output_data(decoder, data);
            }
            return 1;
        }
        else if (sensor_id == ID_WGR800) {
            if (validate_os_checksum(decoder, msg, 17) == 0) {
                float gustWindspeed = (msg[5]&0x0f) /10.0F + ((msg[6]>>4)&0x0f) *1.0F + (msg[6]&0x0f) * 10.0F;
                float avgWindspeed = ((msg[7]>>4)&0x0f) / 10.0F + (msg[7]&0x0f) *1.0F + ((msg[8]>>4)&0x0f) * 10.0F;
                float quadrant = (0x0f&(msg[4]>>4))*22.5F;
                data = data_make(
                        "brand",            "",                     DATA_STRING,    "OS",
                        "model",            "",                     DATA_STRING,    _X("Oregon-WGR800","WGR800"),
                        "id",                 "House Code", DATA_INT,         get_os_rollingcode(msg, sensor_id),
                        "channel",        "Channel",        DATA_INT,         get_os_channel(msg, sensor_id),
                        "battery",        "Battery",        DATA_STRING,    get_os_battery(msg, sensor_id)?"LOW":"OK",
                        "gust",             "Gust",             DATA_FORMAT,    "%2.1f m/s",DATA_DOUBLE, gustWindspeed,
                        "average",        "Average",        DATA_FORMAT,    "%2.1f m/s",DATA_DOUBLE, avgWindspeed,
                        "direction",    "Direction",    DATA_FORMAT,    "%3.1f degrees",DATA_DOUBLE, quadrant,
                        NULL);
                decoder_output_data(decoder, data);
            }
            return 1;
        }
        else if ((msg[0] == 0x20) || (msg[0] == 0x21) || (msg[0] == 0x22) || (msg[0] == 0x23) || (msg[0] == 0x24)) { //    Owl CM160 Readings
            msg[0]=msg[0] & 0x0f;
            if (validate_os_checksum(decoder, msg, 22) == 0) {
                float rawAmp = (msg[4] >> 4 << 8 | (msg[3] & 0x0f )<< 4 | msg[3] >> 4);
                unsigned short int ipower = (rawAmp /(0.27*230)*1000);
                data = data_make(
                        "brand",    "",                     DATA_STRING, "OS",
                        "model",    "",                     DATA_STRING,    _X("Oregon-CM160","CM160"),
                        "id",         "House Code", DATA_INT, msg[1]&0x0F,
                        "power_W", "Power",         DATA_FORMAT,    "%d W", DATA_INT, ipower,
                        NULL);
                    decoder_output_data(decoder, data);
            }
        }
        else if (msg[0] == 0x26) { //    Owl CM180 readings
                msg[0]=msg[0] & 0x0f;
                int valid = validate_os_checksum(decoder, msg, 23);
                int k;
                for (k=0; k<BITBUF_COLS;k++) {    // Reverse nibbles
                    msg[k] = (msg[k] & 0xF0) >> 4 |    (msg[k] & 0x0F) << 4;
                }
                unsigned short int ipower = power(msg);
                unsigned long long itotal = total(msg);
                float total_energy = itotal/3600/1000.0;
                if (itotal && valid == 0) {
                    data = data_make(
                            "brand",            "",                     DATA_STRING, "OS",
                            "model",            "",                     DATA_STRING,    _X("Oregon-CM180","CM180"),
                            "id",                 "House Code", DATA_INT, msg[1]&0x0F,
                            "power_W",        "Power",            DATA_FORMAT,    "%d W",DATA_INT, ipower,
                            "energy_kWh", "Energy",         DATA_FORMAT,    "%2.1f kWh",DATA_DOUBLE, total_energy,
                            NULL);
                    decoder_output_data(decoder, data);
                }
                else if (!itotal) {
                    data = data_make(
                            "brand",    "",                     DATA_STRING, "OS",
                            "model",    "",                     DATA_STRING,    _X("Oregon-CM180","CM180"),
                            "id",         "House Code", DATA_INT, msg[1]&0x0F,
                            "power_W", "Power",         DATA_FORMAT,    "%d W",DATA_INT, ipower,
                            NULL);
                    decoder_output_data(decoder, data);
                }
        }
        else if ((msg[0] != 0) && (msg[1]!= 0)) { //    sync nibble was found    and some data is present...
            if (decoder->verbose) {
                fprintf(stderr, "Message received from unrecognized Oregon Scientific v3 sensor.\n");
                bitrow_printf(msg, bitbuffer->bits_per_row[0], "Message: ");
                bitrow_printf(bb[0], bitbuffer->bits_per_row[0], "Raw: ");
            }
        }
        else if (bb[0][3] != 0 ) {
            if (decoder->verbose) {
                fprintf(stdout, "\nPossible Oregon Scientific v3 message, but sync nibble wasn't found\n");
                bitrow_printf(bb[0], bitbuffer->bits_per_row[0], "Raw Data: ");
            }
        }
    }
    else { // Based on first couple of bytes, either corrupt message or something other than an Oregon Scientific v3 message
        if (decoder->verbose) {
            if (bb[0][3] != 0) {
                fprintf(stdout, "\nUnrecognized Msg in v3: ");
                bitrow_printf(bb[0], bitbuffer->bits_per_row[0], "Raw Data: ");
            }
        }
    }
    return 0;
}

static int oregon_scientific_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = oregon_scientific_v2_1_parser(decoder, bitbuffer);
    if (ret == 0)
        ret = oregon_scientific_v3_parser(decoder, bitbuffer);
    return ret;
}

static char *output_fields[] = {
    "brand",
    "model",
    "id",
    "channel",
    "battery",
    "temperature_C",
    "humidity",
    "rain_rate",
    "rain_total",
    "gust",
    "average",
    "direction",
    "pressure_hPa",
    "uv",
    "power_W",
    "energy_kWh",
    NULL
};

r_device oregon_scientific = {
    .name           = "Oregon Scientific Weather Sensor",
    .modulation     = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_width    = 440, // Nominal 1024Hz (488µs), but pulses are shorter than pauses
    .long_width     = 0, // not used
    .reset_limit    = 2400,
    .decode_fn      = &oregon_scientific_callback,
    .disabled       = 0,
    .fields         = output_fields
};
